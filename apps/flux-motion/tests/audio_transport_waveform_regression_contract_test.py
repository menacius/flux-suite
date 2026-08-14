from pathlib import Path
root = Path(__file__).resolve().parents[1]
runtime = (root / '../../plugins/obs/flux-motion/src/title-audio-runtime.cpp').read_text(encoding='utf-8')
header = (root / '../../plugins/obs/flux-motion/src/title-audio-runtime.h').read_text(encoding='utf-8')
scheduler = (root / 'Core/audio-output-scheduler.h').read_text(encoding='utf-8')
timeline = (root / 'src/timeline/timeline-widget.cpp').read_text(encoding='utf-8')
model = (root / 'Core/layer-model.h').read_text(encoding='utf-8')
data = (root / 'src/core/title-data.cpp').read_text(encoding='utf-8')

assert 'AudioOutputScheduler output_scheduler_' in header
assert 'RealtimeMonitorCadence editor_monitor_cadence_' in header
assert 'AudioOutputSchedulerMode::BufferedSource' in scheduler
assert '160000000ULL, 80000000ULL, 20000000ULL' in scheduler
assert 'AudioOutputSchedulerMode::RealtimeMonitor' in scheduler
assert '10000000ULL, 0ULL, 20000000ULL, 10000000ULL, 1' in scheduler
assert 'timeline_seconds_to_sample(' in scheduler
assert 'class RealtimeMonitorCadence' in scheduler
assert 'next_deadline_ns_ += block_ns_;' in scheduler
assert 'output_scheduler_.is_late(now)' in runtime
assert 'AudioOutputUnderruns' in runtime and 'AudioTimestampRepairs' in runtime
assert 'output_sample_cursor_ = transport_sample_cursor(' in runtime
assert 'advance_transport_cursor(output_sample_cursor_, frames, reverse_)' in runtime
assert 'play_changed || visibility_changed' in runtime
assert 'reported_time_ms_.store(0' in runtime
pump = runtime[runtime.index('void SourceAudioRuntime::pump()'):]
assert 'mix_block(' not in pump.split('int64_t SourceAudioRuntime::duration_ms()', 1)[0]
assert 'audio_waveform_duration' in model and 'audio_waveform_duration' in data
assert 'media_begin' in timeline and 'available_end' in timeline
assert 'audio_in_point' in timeline and 'audio_out_point' in timeline
assert 'std::fmod(timeline_offset, available_span)' in timeline
print('audio transport/waveform regression contract: OK')
