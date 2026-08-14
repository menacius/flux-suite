#include "standalone-editor-host.h"

#include "title-data.h"
#include "title-audio-runtime.h"
#include "title-preferences.h"
#include "title-video-runtime.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#if defined(FXM_HAVE_QT_MULTIMEDIA)
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSink>
#include <QByteArray>
#include <QIODevice>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMutex>
#include <QMutexLocker>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace fxm::editor {
namespace {

class EmptyHostEventSubscription final
    : public editor_host::IHostEventSubscription {};
class EmptyProgramCommandSubscription final
    : public editor_host::IProgramCommandSubscription {};

#if defined(FXM_HAVE_QT_MULTIMEDIA)

QAudioDevice selected_preview_audio_device()
{
    const QByteArray wanted =
        TitlePreferences::preview_audio_device_id().toUtf8();
    if (!wanted.isEmpty()) {
        for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
            if (device.id() == wanted)
                return device;
        }
    }
    return QMediaDevices::defaultAudioOutput();
}

class StandaloneAudioRingDevice final : public QIODevice {
public:
    StandaloneAudioRingDevice() { open(QIODevice::ReadOnly); }

    void set_capacity(qsizetype capacity)
    {
        QMutexLocker locker(&mutex_);
        capacity_ = std::max<qsizetype>(4096, capacity);
    }

    void enqueue(const QByteArray &data)
    {
        if (data.isEmpty())
            return;
        {
            QMutexLocker locker(&mutex_);
            buffer_.append(data);
            if (buffer_.size() > capacity_)
                buffer_.remove(0, buffer_.size() - capacity_);
        }
        emit readyRead();
    }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override
    {
        QMutexLocker locker(&mutex_);
        return static_cast<qint64>(buffer_.size()) +
            QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 max_size) override
    {
        if (!data || max_size <= 0)
            return 0;
        QMutexLocker locker(&mutex_);
        const qsizetype copied = std::min<qsizetype>(
            buffer_.size(), static_cast<qsizetype>(max_size));
        if (copied > 0) {
            std::memcpy(data, buffer_.constData(),
                        static_cast<std::size_t>(copied));
            buffer_.remove(0, copied);
        }
        return static_cast<qint64>(copied);
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    mutable QMutex mutex_;
    QByteArray buffer_;
    qsizetype capacity_ = 192000;
};

class StandaloneAudioBackend final : public audio::IAudioBackend {
public:
    explicit StandaloneAudioBackend(const QAudioDevice &requested_device)
    {
        const QAudioDevice device = requested_device.isNull()
            ? QMediaDevices::defaultAudioOutput() : requested_device;
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        if (!device.isNull() && !device.isFormatSupported(format))
            format = device.preferredFormat();
        if (format.sampleRate() < 8000)
            format.setSampleRate(48000);
        if (format.channelCount() < 1)
            format.setChannelCount(2);
        format_ = format;
        sample_rate_ = static_cast<std::uint32_t>(format_.sampleRate());
        const int bytes_per_frame = std::max(1, format_.bytesPerFrame());
        ring_.set_capacity(static_cast<qsizetype>(sample_rate_) *
                           bytes_per_frame / 2);
        sink_ = std::make_unique<QAudioSink>(device, format_);
        sink_->setBufferSize(static_cast<int>(sample_rate_) *
                             bytes_per_frame / 10);
        sink_->setVolume(1.0f);
        sink_->start(&ring_);
    }

    std::uint32_t sample_rate() const noexcept override
    {
        return sample_rate_;
    }

    bool submit(const audio::AudioPacketView &packet,
                std::string *error) override
    {
        if (error)
            error->clear();
        if (!packet.valid() || packet.sample_rate != sample_rate_ ||
            packet.channel_layout != audio::AudioChannelLayout::Stereo) {
            if (error)
                *error = "Unsupported standalone audio packet.";
            return false;
        }

        const int channels = std::max(1, format_.channelCount());
        const int bytes_per_sample = format_.bytesPerSample();
        if (bytes_per_sample <= 0) {
            if (error)
                *error = "Unsupported standalone audio device format.";
            return false;
        }
        QByteArray interleaved;
        interleaved.resize(
            static_cast<qsizetype>(packet.frames) * channels *
            bytes_per_sample);
        char *destination = interleaved.data();
        for (std::uint32_t frame = 0; frame < packet.frames; ++frame) {
            const float left = std::clamp(packet.planes[0][frame], -1.0f, 1.0f);
            const float right = std::clamp(packet.planes[1][frame], -1.0f, 1.0f);
            for (int channel = 0; channel < channels; ++channel) {
                const float sample = channels == 1
                    ? (left + right) * 0.5f
                    : (channel == 0 ? left : channel == 1 ? right : 0.0f);
                switch (format_.sampleFormat()) {
                case QAudioFormat::Float:
                    std::memcpy(destination, &sample, sizeof(sample));
                    break;
                case QAudioFormat::Int16: {
                    const std::int16_t value = static_cast<std::int16_t>(
                        std::lrint(sample * 32767.0f));
                    std::memcpy(destination, &value, sizeof(value));
                    break;
                }
                case QAudioFormat::Int32: {
                    const std::int32_t value = static_cast<std::int32_t>(
                        std::llround(static_cast<double>(sample) *
                                     2147483647.0));
                    std::memcpy(destination, &value, sizeof(value));
                    break;
                }
                case QAudioFormat::UInt8:
                    *reinterpret_cast<std::uint8_t *>(destination) =
                        static_cast<std::uint8_t>(
                            std::lrint((sample + 1.0f) * 127.5f));
                    break;
                default:
                    if (error)
                        *error = "Unsupported standalone sample format.";
                    return false;
                }
                destination += bytes_per_sample;
            }
        }
        ring_.enqueue(interleaved);
        return true;
    }

    void set_monitoring(bool enabled)
    {
        if (sink_)
            sink_->setVolume(enabled ? 1.0f : 0.0f);
    }

private:
    QAudioFormat format_;
    std::uint32_t sample_rate_ = 48000;
    StandaloneAudioRingDevice ring_;
    std::unique_ptr<QAudioSink> sink_;
};

class StandaloneAudioPreviewSession final
    : public editor_host::IEditorAudioPreviewSession {
public:
    explicit StandaloneAudioPreviewSession(std::string title_id)
        : title_id_(std::move(title_id)),
          name_("Flux Motion standalone preview")
    {
        auto backend = std::make_unique<StandaloneAudioBackend>(
            selected_preview_audio_device());
        backend_ = backend.get();
        runtime_ = std::make_unique<audio::SourceAudioRuntime>(
            std::move(backend), name_, true);
    }

    ~StandaloneAudioPreviewSession() override { stop(); }

    const std::string &name() const noexcept override { return name_; }
    bool active() const noexcept override { return true; }
    bool showing() const noexcept override { return true; }
    int channel_count() const noexcept override { return 2; }

    void set_meter_handler(editor_host::AudioMeterHandler handler) override
    {
        meter_handler_ = std::move(handler);
    }

    void set_monitoring(bool enabled) override
    {
        monitoring_ = enabled;
        if (backend_)
            backend_->set_monitoring(enabled);
    }

    int monitoring_mode() const noexcept override
    {
        return monitoring_ ? 1 : 0;
    }

    void set_title_snapshot(const std::shared_ptr<Title> &title) override
    {
        title_ = title;
        if (runtime_)
            runtime_->bind_title(title_, 0);
    }

    void set_transport(double playhead, bool reverse,
                       double speed_factor) override
    {
        const bool direction_changed = reverse_ != reverse;
        playhead_ = std::max(0.0, playhead);
        reverse_ = reverse;
        speed_factor_ = std::clamp(speed_factor, 0.02, 4.0);
        if (runtime_)
            runtime_->transport(playhead_, playing_, true,
                                direction_changed, reverse_, speed_factor_);
    }

    void seek(double playhead) override
    {
        playhead_ = std::max(0.0, playhead);
        if (runtime_)
            runtime_->transport(playhead_, playing_, true, true,
                                reverse_, speed_factor_);
    }

    void set_playing(bool playing) override
    {
        playing_ = playing;
        if (runtime_)
            runtime_->transport(playhead_, playing_, true, false,
                                reverse_, speed_factor_);
    }

    void stop() override
    {
        playing_ = false;
        if (runtime_)
            runtime_->stop(false);
    }

    editor_host::MediaState media_state() const noexcept override
    {
        if (!runtime_)
            return editor_host::MediaState::None;
        switch (runtime_->media_state()) {
        case OBS_MEDIA_STATE_PLAYING:
            return editor_host::MediaState::Playing;
        case OBS_MEDIA_STATE_PAUSED:
            return editor_host::MediaState::Paused;
        case OBS_MEDIA_STATE_STOPPED:
        case OBS_MEDIA_STATE_ENDED:
            return editor_host::MediaState::Stopped;
        case OBS_MEDIA_STATE_ERROR:
            return editor_host::MediaState::Error;
        default:
            return editor_host::MediaState::None;
        }
    }

    double media_time() const noexcept override
    {
        return runtime_
            ? static_cast<double>(runtime_->time_ms()) / 1000.0
            : playhead_;
    }

    bool audio_levels(float *left, float *right,
                      std::uint64_t *last_update_ns) const override
    {
        if (!runtime_) {
            if (left) *left = 0.0f;
            if (right) *right = 0.0f;
            if (last_update_ns) *last_update_ns = 0;
            return false;
        }
        runtime_->current_levels(left, right, last_update_ns);
        return true;
    }

private:
    struct Entry {
        std::string layer_id;
        std::string source;
        int audio_track = 0;
        std::unique_ptr<QAudioOutput> output;
        std::unique_ptr<QMediaPlayer> player;
        QElapsedTimer seek_clock;
        QElapsedTimer recovery_clock;
        qint64 pending_position = -1;
        qint64 last_wanted_position = -1;
    };

    static std::string media_signature(const std::shared_ptr<Title> &title)
    {
        if (!title)
            return {};
        std::string result;
        for (const auto &layer : title->layers) {
            if (!layer || layer->type != LayerType::Audio)
                continue;
            result += layer->id + '|' + layer->audio_source + '|' +
                layer->linked_media_layer_id + '|' +
                std::to_string(layer->audio_stream_index) + ';';
        }
        return result;
    }

    int audio_track_ordinal(const Layer &audio) const
    {
        if (!title_ || !audio.linked_media_stream)
            return 0;
        int ordinal = 0;
        for (const auto &candidate : title_->layers) {
            if (!candidate || candidate->type != LayerType::Audio ||
                !candidate->linked_media_stream ||
                candidate->linked_media_layer_id !=
                    audio.linked_media_layer_id)
                continue;
            if (candidate->id == audio.id)
                return ordinal;
            ++ordinal;
        }
        return 0;
    }

    std::string media_path(const Layer &audio) const
    {
        if (title_ && audio.linked_media_stream &&
            !audio.linked_media_layer_id.empty()) {
            const auto video = title_->find_layer(
                audio.linked_media_layer_id);
            if (video && video->type == LayerType::Video)
                return video->video_source;
        }
        return audio.audio_source;
    }

    bool layer_visible(const Layer &layer) const
    {
        if (!title_ || !layer.visible)
            return false;
        std::unordered_set<std::string> visited;
        std::string parent_id = layer.parent_id;
        while (!parent_id.empty() && visited.insert(parent_id).second) {
            const auto parent = title_->find_layer(parent_id);
            if (!parent)
                break;
            if (!parent->visible)
                return false;
            parent_id = parent->parent_id;
        }
        return true;
    }

    const Layer *effective_controls(const Layer &audio) const
    {
        if (title_ && audio.linked_media_stream &&
            !audio.linked_media_layer_id.empty()) {
            const auto video = title_->find_layer(
                audio.linked_media_layer_id);
            if (video && video->type == LayerType::Video)
                return video.get();
        }
        return &audio;
    }

    qint64 media_position_ms(const Layer &audio) const
    {
        const Layer *controls = effective_controls(audio);
        if (!controls)
            return 0;
        const bool linked_video = controls->type == LayerType::Video;
        const double timeline_in = controls->in_time;
        const double elapsed = std::max(0.0, playhead_ - timeline_in);
        const double media_in = linked_video
            ? controls->video_in_point : audio.audio_in_point;
        double media_out = linked_video
            ? controls->video_out_point : audio.audio_out_point;
        if (media_out <= media_in)
            media_out = media_in + std::max(
                0.0, controls->out_time - controls->in_time);
        const double span = std::max(0.0, media_out - media_in);
        const bool loop = linked_video
            ? (controls->video_loop || controls->audio_loop ||
               controls->audio_playback_mode == AudioPlaybackMode::Loop)
            : (audio.audio_loop ||
               audio.audio_playback_mode == AudioPlaybackMode::Loop);
        double media_time = media_in + elapsed;
        if (linked_video && controls->video_time_remap_enabled &&
            controls->video_time_remap_audio_mode !=
                VideoTimeRemapAudioMode::PreserveLinearClipAudio) {
            media_time = fxm::video::evaluate_video_time_remap(
                *controls, elapsed).source_time;
        } else if (loop && span > 0.000001) {
            media_time = media_in + std::fmod(elapsed, span);
        }
        media_time = std::clamp(media_time, media_in,
            media_out > media_in ? media_out : media_time);
        return static_cast<qint64>(std::llround(media_time * 1000.0));
    }

    void rebuild_players()
    {
        entries_.clear();
        if (!title_)
            return;
        const QAudioDevice device = selected_preview_audio_device();
        for (const auto &layer : title_->layers) {
            if (!layer || layer->type != LayerType::Audio)
                continue;
            const std::string path = media_path(*layer);
            if (path.empty() || !QFileInfo::exists(
                    QString::fromStdString(path)))
                continue;
            auto entry = std::make_unique<Entry>();
            entry->layer_id = layer->id;
            entry->source = path;
            entry->audio_track = audio_track_ordinal(*layer);
            entry->output = std::make_unique<QAudioOutput>();
            if (!device.isNull())
                entry->output->setDevice(device);
            entry->output->setMuted(!monitoring_);
            entry->player = std::make_unique<QMediaPlayer>();
            entry->player->setAudioOutput(entry->output.get());
            entry->player->setSource(QUrl::fromLocalFile(
                QString::fromStdString(path)));
            entry->player->setActiveAudioTrack(entry->audio_track);
            entry->seek_clock.start();
            entry->recovery_clock.start();
            entries_.push_back(std::move(entry));
        }
    }

    static bool media_is_seekable(const QMediaPlayer &player)
    {
        switch (player.mediaStatus()) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferingMedia:
        case QMediaPlayer::BufferedMedia:
        case QMediaPlayer::StalledMedia:
        case QMediaPlayer::EndOfMedia:
            return true;
        default:
            return false;
        }
    }

    qint64 synchronize_position(Entry &entry, qint64 wanted, bool force_seek)
    {
        const bool media_mapping_jump = entry.last_wanted_position >= 0 &&
            std::abs(wanted - entry.last_wanted_position) > 400;
        entry.last_wanted_position = wanted;
        if (!media_is_seekable(*entry.player)) {
            entry.pending_position = wanted;
            return 0;
        }

        const bool pending_seek = entry.pending_position >= 0;
        if (pending_seek)
            wanted = entry.pending_position;
        const qint64 signed_drift = wanted - entry.player->position();
        const qint64 drift = std::abs(signed_drift);

        /* QMediaPlayer/WMF is already clocked. Correct only a material drift,
         * and rate-limit corrections while playing. The previous 180 ms
         * per-frame correction repeatedly flushed decoder/audio buffers and
         * eventually exhausted the Windows audio pipeline. */
        const bool paused_seek = !playing_ && drift > 20;
        const bool playing_seek = playing_ && drift > 400 &&
            entry.seek_clock.elapsed() >= 750;
        if (force_seek || media_mapping_jump || pending_seek || paused_seek ||
            playing_seek) {
            entry.player->setPosition(wanted);
            entry.pending_position = -1;
            entry.seek_clock.restart();
            return 0;
        }
        return signed_drift;
    }

    void update_players(bool force_seek)
    {
        if (!title_)
            return;
        bool any_solo = false;
        for (const auto &layer : title_->layers) {
            if (layer && layer->type == LayerType::Audio)
                any_solo = any_solo || layer->audio_solo;
        }
        for (auto &entry : entries_) {
            if (!entry || !entry->player || !entry->output)
                continue;
            const auto audio = title_->find_layer(entry->layer_id);
            if (!audio || audio->type != LayerType::Audio)
                continue;
            if (entry->player->error() != QMediaPlayer::NoError) {
                if (entry->recovery_clock.elapsed() < 3000)
                    continue;
                /* Re-open only the failed decoder, with backoff. This lets a
                 * transient device/decoder failure recover without rebuilding
                 * every layer and without a tight resource-allocation loop. */
                entry->player->stop();
                entry->player->setSource(QUrl());
                entry->player->setSource(QUrl::fromLocalFile(
                    QString::fromStdString(entry->source)));
                entry->pending_position = media_position_ms(*audio);
                entry->recovery_clock.restart();
            }
            if (entry->player->audioTracks().size() > entry->audio_track &&
                entry->player->activeAudioTrack() != entry->audio_track) {
                entry->player->setActiveAudioTrack(entry->audio_track);
            }
            const Layer *controls = effective_controls(*audio);
            const bool in_range = controls &&
                playhead_ >= controls->in_time &&
                playhead_ < controls->out_time;
            const bool muted = !controls || controls->audio_muted ||
                (any_solo && !audio->audio_solo);
            const double local_time = controls
                ? std::max(0.0, playhead_ - controls->in_time) : 0.0;
            const float gain = controls
                ? static_cast<float>(std::clamp(
                    controls->audio_volume_prop.evaluate(local_time),
                    0.0, 1.0))
                : 0.0f;
            entry->output->setVolume(muted ? 0.0f : gain);
            entry->output->setMuted(!monitoring_);
            const qint64 wanted = media_position_ms(*audio);
            const qint64 signed_drift =
                synchronize_position(*entry, wanted, force_seek);
            double corrected_rate = speed_factor_;
            if (playing_ && !force_seek) {
                /* Keep small decoder drift inaudible by correcting it with a
                 * bounded rate nudge. Large discontinuities still seek, but
                 * never on every rendered frame. This also lets Play Every
                 * Frame follow slow visual cadence without falling behind. */
                const double drift_correction = std::clamp(
                    static_cast<double>(signed_drift) / 1000.0 * 0.35,
                    -0.12, 0.12);
                corrected_rate = std::clamp(
                    speed_factor_ + drift_correction, 0.02, 4.0);
            }
            if (std::abs(entry->player->playbackRate() - corrected_rate) >
                0.005)
                entry->player->setPlaybackRate(corrected_rate);
            const bool should_play = playing_ && !reverse_ && in_range &&
                layer_visible(*audio);
            if (should_play) {
                if (entry->player->playbackState() !=
                    QMediaPlayer::PlayingState)
                    entry->player->play();
            } else if (entry->player->playbackState() ==
                       QMediaPlayer::PlayingState) {
                entry->player->pause();
            }
        }
    }

    std::string title_id_;
    std::string name_;
    std::shared_ptr<Title> title_;
    std::string media_signature_;
    std::vector<std::unique_ptr<Entry>> entries_;
    editor_host::AudioMeterHandler meter_handler_;
    double playhead_ = 0.0;
    double speed_factor_ = 1.0;
    bool reverse_ = false;
    bool playing_ = false;
    bool monitoring_ = true;
    StandaloneAudioBackend *backend_ = nullptr;
    std::unique_ptr<audio::SourceAudioRuntime> runtime_;
};

#endif

QString resolved_data_root(const QString &explicit_root)
{
    const QString requested = explicit_root.trimmed();
    if (!requested.isEmpty())
        return QFileInfo(requested).absoluteFilePath();

    const QString installed = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("data"));
    if (QDir(installed).exists())
        return QDir(installed).absolutePath();

#if defined(FXM_STANDALONE_DATA_DIR)
    const QString development = QString::fromUtf8(FXM_STANDALONE_DATA_DIR);
    if (QDir(development).exists())
        return QDir(development).absolutePath();
#endif
    /* Return the installed location even when incomplete so diagnostics and
     * missing-file errors point at the deployment that must be repaired. */
    return QDir(installed).absolutePath();
}

} // namespace

QString resolve_standalone_data_root(const QString &explicit_root)
{
    return resolved_data_root(explicit_root);
}

void *StandaloneEditorHost::main_window_handle() const noexcept
{
    return window_;
}

std::vector<editor_host::TitleSourceBinding>
StandaloneEditorHost::title_sources() const
{
    return {};
}

int StandaloneEditorHost::remove_title_sources(
    const std::vector<std::string> &)
{
    return 0;
}

bool StandaloneEditorHost::add_title_to_current_scene(
    const std::string &, const std::string &, std::string *error)
{
    if (error)
        *error = "Scene insertion is available when an OBS plugin host is connected.";
    return false;
}

bool StandaloneEditorHost::open_title_in_editor(
    const std::string &, std::string *error)
{
    if (error)
        *error = "The title is already open in the standalone Editor.";
    return false;
}

bool StandaloneEditorHost::open_editor_preferences(std::string *error)
{
    if (error)
        *error = "Preferences are already available in the standalone Editor.";
    return false;
}

std::unique_ptr<editor_host::IHostEventSubscription>
StandaloneEditorHost::subscribe(editor_host::HostEventHandler)
{
    return std::make_unique<EmptyHostEventSubscription>();
}

std::unique_ptr<editor_host::IProgramCommandSubscription>
StandaloneEditorHost::subscribe_program_commands(std::function<void(
    editor_host::ProgramCommand, const std::string &)>)
{
    return std::make_unique<EmptyProgramCommandSubscription>();
}

std::unique_ptr<editor_host::ILiveCuePreviewSession>
StandaloneEditorHost::create_live_cue_preview_session()
{
    return nullptr;
}

std::unique_ptr<editor_host::IEditorAudioPreviewSession>
StandaloneEditorHost::create_editor_audio_preview_session(
    const std::string &title_id)
{
#if defined(FXM_HAVE_QT_MULTIMEDIA)
    return std::make_unique<StandaloneAudioPreviewSession>(title_id);
#else
    (void)title_id;
    return nullptr;
#endif
}

editor_host::AudioMeterConfiguration
StandaloneEditorHost::audio_meter_configuration() const
{
    return {};
}

std::vector<editor_host::AudioOutputDeviceInfo>
StandaloneEditorHost::audio_output_devices() const
{
    std::vector<editor_host::AudioOutputDeviceInfo> result;
#if defined(FXM_HAVE_QT_MULTIMEDIA)
    const QAudioDevice default_device = QMediaDevices::defaultAudioOutput();
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        result.push_back({device.id().toStdString(),
                          device.description().toStdString(),
                          !default_device.isNull() &&
                              device.id() == default_device.id()});
    }
#endif
    return result;
}

StandaloneAssetPathProvider::StandaloneAssetPathProvider(
    std::string data_root)
    : data_root_(resolve_standalone_data_root(
          QString::fromStdString(data_root)).toStdString())
{
}

std::string StandaloneAssetPathProvider::resolve_asset_path(
    std::string_view relative_path) const
{
    const QString root = QString::fromStdString(data_root_);
    return QDir(root).filePath(
        QString::fromUtf8(relative_path.data(),
                          static_cast<int>(relative_path.size())))
        .toStdString();
}

StandaloneHostContextProvider::StandaloneHostContextProvider(
    std::string config_root, std::string project_scope)
    : config_root_(std::move(config_root)),
      project_scope_(std::move(project_scope))
{
}

std::string StandaloneHostContextProvider::config_path(
    std::string_view relative_path) const
{
    const QString root = config_root_.empty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        : QString::fromStdString(config_root_);
    QDir().mkpath(root);
    return QDir(root).filePath(
        QString::fromUtf8(relative_path.data(),
                          static_cast<int>(relative_path.size())))
        .toStdString();
}

std::string StandaloneHostContextProvider::project_scope_name() const
{
    return project_scope_.empty() ? "Standalone Editor" : project_scope_;
}

void StandaloneLogger::log(LogLevel level, const std::string &category,
                           const std::string &message)
{
    const QString line = QStringLiteral("[%1] %2")
        .arg(QString::fromStdString(category), QString::fromStdString(message));
    if (level == LogLevel::Error)
        qCritical().noquote() << line;
    else if (level == LogLevel::Warning)
        qWarning().noquote() << line;
    else
        qInfo().noquote() << line;
}

struct StandaloneTranslationProvider::Storage {
    std::unordered_map<std::string, std::string> values;
};

StandaloneTranslationProvider::StandaloneTranslationProvider(
    std::string data_root)
    : storage_(std::make_shared<Storage>())
{
    const QString root = resolve_standalone_data_root(
        QString::fromStdString(data_root));
    QSettings locale(QDir(root).filePath(
        QStringLiteral("locale/en-US.ini")), QSettings::IniFormat);
    for (const QString &key : locale.allKeys())
        storage_->values.emplace(key.toStdString(),
                                 locale.value(key).toString().toStdString());
}

const char *StandaloneTranslationProvider::translate(
    const char *key) const noexcept
{
    if (!key)
        return "";
    const auto found = storage_->values.find(key);
    return found == storage_->values.end() ? key : found->second.c_str();
}

} // namespace fxm::editor
