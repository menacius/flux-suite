#include "audio-output-scheduler.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "audio output scheduler failure: " << message << '\n';
    return false;
}

std::vector<std::uint64_t> fill_once(
    fxm::audio::AudioOutputScheduler &scheduler, std::uint64_t now)
{
    std::vector<std::uint64_t> timestamps;
    for (int emitted = 0;
         emitted < scheduler.maximum_blocks_per_wake() &&
         scheduler.should_fill(now);
         ++emitted) {
        timestamps.push_back(scheduler.next_timestamp_ns());
        scheduler.advance(480, 48000);
    }
    return timestamps;
}

} // namespace

int main()
{
    using fxm::audio::AudioOutputScheduler;
    using fxm::audio::AudioOutputSchedulerMode;
    using fxm::audio::RealtimeMonitorCadence;
    using fxm::audio::timeline_seconds_to_sample;

    bool ok = true;
    ok &= expect(timeline_seconds_to_sample(0.0, 48000) == 0,
                 "zero time maps to zero samples");
    ok &= expect(timeline_seconds_to_sample(2.5, 48000) == 120000,
                 "timeline seconds map to the exact 48 kHz sample");
    ok &= expect(timeline_seconds_to_sample(1.0 / 60.0, 48000) == 800,
                 "one 60 fps frame maps to 800 samples");

    constexpr std::uint64_t start = 1000000000ULL;
    AudioOutputScheduler scheduler(AudioOutputSchedulerMode::BufferedSource);
    scheduler.reset(start);

    std::vector<std::uint64_t> timestamps;
    while (scheduler.should_fill(start)) {
        timestamps.push_back(scheduler.next_timestamp_ns());
        scheduler.advance(480, 48000);
    }

    ok &= expect(timestamps.size() == 16,
                 "buffered source target lead contains sixteen 10 ms blocks");
    for (std::size_t index = 1; index < timestamps.size(); ++index) {
        ok &= expect(timestamps[index] - timestamps[index - 1] == 10000000ULL,
                     "buffered output packet timestamps are gapless");
    }
    ok &= expect(scheduler.next_timestamp_ns() ==
                     start + scheduler.profile().target_lead_ns,
                 "buffered scheduler stops exactly at the high watermark");
    ok &= expect(scheduler.wait_ns(start) ==
                     scheduler.profile().maximum_wait_ns,
                 "buffered worker sleeps only in bounded increments");

    const std::uint64_t low_watermark_time =
        start + scheduler.profile().target_lead_ns -
        scheduler.profile().low_watermark_ns;
    ok &= expect(scheduler.wait_ns(low_watermark_time) == 0,
                 "buffered worker refills immediately at the low watermark");

    ok &= expect(!scheduler.is_late(
                     scheduler.next_timestamp_ns() +
                     scheduler.profile().late_tolerance_ns),
                 "buffered late tolerance boundary is not an underrun");
    ok &= expect(scheduler.is_late(
                     scheduler.next_timestamp_ns() +
                     scheduler.profile().late_tolerance_ns + 1),
                 "a real buffered scheduling underrun is detected");

    const std::uint64_t repaired_now = start + 500000000ULL;
    scheduler.reset(repaired_now);
    ok &= expect(scheduler.next_timestamp_ns() == repaired_now,
                 "underrun repair re-anchors the output clock without a gap");

    scheduler.clear();
    ok &= expect(!scheduler.initialized(),
                 "pause/seek clears scheduler state");

    /* Monitor-only output is consumed immediately by OBS's monitoring backend,
     * not by the timestamped source mixer. It must never submit the normal
     * 160 ms queue as one callback burst. */
    scheduler.set_mode(AudioOutputSchedulerMode::RealtimeMonitor);
    scheduler.reset(start);
    const auto monitor_initial = fill_once(scheduler, start);
    ok &= expect(monitor_initial.size() == 1,
                 "monitor profile emits exactly one 10 ms block per wake");
    ok &= expect(scheduler.next_timestamp_ns() == start + 10000000ULL,
                 "monitor profile advances by one packet only");
    ok &= expect(scheduler.maximum_blocks_per_wake() == 1,
                 "monitor profile forbids catch-up bursts");
    ok &= expect(scheduler.profile().target_lead_ns == 10000000ULL,
                 "monitor profile has only one packet of nominal lead");

    const std::uint64_t monitor_refill_time = start + 10000000ULL;
    const auto monitor_refill = fill_once(scheduler, monitor_refill_time);
    ok &= expect(monitor_refill.size() == 1,
                 "each monitor wake emits one additional packet");
    ok &= expect(monitor_refill.front() == start + 10000000ULL,
                 "monitor timestamps remain contiguous");

    /* The editor monitor uses a separate sample-locked wall-clock cadence.
     * Reproduce the v185 failure: a 0.35 ms timer oversleep on every packet
     * must not accumulate into periodic 20 ms timestamp repairs. */
    RealtimeMonitorCadence monitor_cadence(10000000ULL, 3, 70000000ULL);
    monitor_cadence.reset(start);
    std::uint64_t monitor_previous_ts = 0;
    for (int packet = 0; packet < 3; ++packet) {
        ok &= expect(monitor_cadence.ready(start),
                     "monitor prefill packets are immediately ready");
        const std::uint64_t ts = monitor_cadence.take_timestamp();
        if (monitor_previous_ts != 0) {
            ok &= expect(ts - monitor_previous_ts == 10000000ULL,
                         "monitor prefill timestamps are contiguous");
        }
        monitor_previous_ts = ts;
    }
    ok &= expect(monitor_cadence.next_deadline_ns() == start + 10000000ULL,
                 "monitor first paced deadline is anchored to reset time");

    std::uint64_t monitor_now = start;
    for (int packet = 0; packet < 900; ++packet) {
        monitor_now = monitor_cadence.next_deadline_ns() + 350000ULL;
        ok &= expect(monitor_cadence.ready(monitor_now),
                     "overslept monitor packet is ready");
        ok &= expect(!monitor_cadence.hard_resync_needed(monitor_now),
                     "sub-millisecond oversleep never accumulates");
        const std::uint64_t ts = monitor_cadence.take_timestamp();
        ok &= expect(ts - monitor_previous_ts == 10000000ULL,
                     "nine-second monitor simulation has no timestamp gaps");
        monitor_previous_ts = ts;
    }
    ok &= expect(monitor_cadence.next_deadline_ns() ==
                     start + 9010000000ULL,
                 "absolute monitor deadlines do not absorb wake-up oversleep");

    /* A moderate late wake is repaired by a small contiguous catch-up, not by
     * jumping the packet timestamp to wall clock. */
    monitor_now = monitor_cadence.next_deadline_ns() + 18000000ULL;
    int catchup_packets = 0;
    while (monitor_cadence.ready(monitor_now) && catchup_packets < 4) {
        const std::uint64_t ts = monitor_cadence.take_timestamp();
        ok &= expect(ts - monitor_previous_ts == 10000000ULL,
                     "catch-up packets remain timestamp-contiguous");
        monitor_previous_ts = ts;
        ++catchup_packets;
    }
    ok &= expect(catchup_packets == 2,
                 "18 ms late wake requires exactly two contiguous packets");
    ok &= expect(!monitor_cadence.hard_resync_needed(monitor_now),
                 "bounded catch-up avoids a hard monitor resync");

    const std::uint64_t hard_stall_now =
        monitor_cadence.next_deadline_ns() + 71000000ULL;
    ok &= expect(monitor_cadence.hard_resync_needed(hard_stall_now),
                 "only a real 70 ms monitor stall requests hard resync");
    monitor_cadence.reset(hard_stall_now);
    ok &= expect(!monitor_cadence.hard_resync_needed(hard_stall_now),
                 "hard monitor resync resets cadence state");

    /* Switching back to program output must reset the clock so monitor timing
     * never leaks into the normal source queue. */
    scheduler.set_mode(AudioOutputSchedulerMode::BufferedSource);
    ok &= expect(!scheduler.initialized(),
                 "changing output consumer clears scheduler state");

    /* Ten-minute deterministic jitter simulation for the buffered source path.
     * Uneven worker wakeups must not introduce timestamp holes or false
     * underruns while the bounded queue remains ahead of the consumer. */
    constexpr std::uint64_t ten_minutes_ns = 600ULL * 1000000000ULL;
    constexpr std::uint64_t jitter_pattern[] = {
        1000000ULL, 7000000ULL, 13000000ULL, 4000000ULL,
        21000000ULL, 9000000ULL, 33000000ULL, 5000000ULL};
    std::uint64_t simulated_now = start;
    std::uint64_t previous_timestamp = 0;
    std::uint64_t emitted_blocks = 0;
    scheduler.reset(simulated_now);
    for (std::size_t wake = 0; simulated_now < start + ten_minutes_ns; ++wake) {
        ok &= expect(!scheduler.is_late(simulated_now),
                     "bounded desktop jitter must not underrun");
        while (scheduler.should_fill(simulated_now)) {
            const std::uint64_t timestamp = scheduler.next_timestamp_ns();
            if (previous_timestamp != 0) {
                ok &= expect(timestamp - previous_timestamp == 10000000ULL,
                             "ten-minute simulation emitted a timestamp gap");
            }
            previous_timestamp = timestamp;
            scheduler.advance(480, 48000);
            ++emitted_blocks;
        }
        simulated_now += jitter_pattern[
            wake % (sizeof(jitter_pattern) / sizeof(jitter_pattern[0]))];
    }
    ok &= expect(emitted_blocks >= 60000,
                 "ten-minute simulation produced the expected audio duration");

    simulated_now = scheduler.next_timestamp_ns() +
                    scheduler.profile().late_tolerance_ns + 250000000ULL;
    ok &= expect(scheduler.is_late(simulated_now),
                 "long worker stall must be reported as an underrun");
    scheduler.reset(simulated_now);
    ok &= expect(!scheduler.is_late(simulated_now),
                 "repaired scheduler resumes from a continuous clock anchor");

    if (ok)
        std::cout << "audio output scheduler test passed\n";
    return ok ? 0 : 1;
}
