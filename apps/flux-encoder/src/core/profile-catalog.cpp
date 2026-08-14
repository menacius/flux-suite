#include "core/profile-catalog.h"
#include "core/database.h"

#include <QSet>
#include <tuple>
#include <utility>

namespace flux {
namespace {
const QString kBroadcast = QStringLiteral("builtin-broadcast");
const QString kWeb = QStringLiteral("builtin-web");
const QString kEditing = QStringLiteral("builtin-editing");
const QString kProxy = QStringLiteral("builtin-proxy");
const QString kArchive = QStringLiteral("builtin-archive");
const QString kHardware = QStringLiteral("builtin-hardware");
const QString kNvidia = QStringLiteral("builtin-nvidia");
const QString kIntel = QStringLiteral("builtin-intel");
const QString kAmd = QStringLiteral("builtin-amd");
const QString kVaapi = QStringLiteral("builtin-vaapi");
const QString kAudio = QStringLiteral("builtin-audio");
const QString kUser = QStringLiteral("user-profiles");

RenderProfile base(QString uuid, QString name, QString category, QString encoder,
                   QString backend, QString extension = QStringLiteral("mp4"))
{
    RenderProfile p;
    p.uuid = std::move(uuid);
    p.name = std::move(name);
    p.categoryUuid = std::move(category);
    p.videoEncoder = std::move(encoder);
    p.hardwareBackend = std::move(backend);
    p.extension = extension;
    p.container = extension;
    p.builtIn = true;
    p.quality = 19;
    p.audioEncoder = QStringLiteral("aac");
    p.audioBitrateKbps = 192;
    p.allowSoftwareFallback = p.hardwareBackend != QStringLiteral("software");
    return p;
}

void applyAvailability(RenderProfile &p, const FfmpegCapabilities &caps)
{
    if (p.hardwareBackend == QStringLiteral("software")) {
        const bool videoAvailable = p.videoEncoder.isEmpty() || caps.hasEncoder(p.videoEncoder);
        const bool audioAvailable = p.audioEncoder.isEmpty() || caps.hasEncoder(p.audioEncoder);
        p.enabled = videoAvailable && audioAvailable;
        if (!videoAvailable)
            p.unavailableReason = QStringLiteral("Required video encoder is missing from FFmpeg: %1").arg(p.videoEncoder);
        else if (!audioAvailable)
            p.unavailableReason = QStringLiteral("Required audio encoder is missing from FFmpeg: %1").arg(p.audioEncoder);
        else
            p.unavailableReason.clear();
        return;
    }
    p.enabled = caps.encoderUsable(p.videoEncoder);
    p.unavailableReason = p.enabled ? QString() : caps.encoderReason(p.videoEncoder);
}

void addProfile(QList<RenderProfile> &out, RenderProfile profile, const FfmpegCapabilities &caps)
{
    applyAvailability(profile, caps);
    out.push_back(profile);
}

RenderProfile h264Profile(const QString &uuid, const QString &name, const QString &category,
                          int width, int height, double fps, int bitrate, int quality = -1)
{
    RenderProfile p = base(uuid, name, category, QStringLiteral("libx264"), QStringLiteral("software"));
    p.width = width;
    p.height = height;
    p.frameRate = fps;
    p.videoBitrateKbps = bitrate;
    p.quality = quality;
    p.encoderPreset = bitrate > 0 ? QStringLiteral("medium") : QStringLiteral("slow");
    p.muxerArguments = {QStringLiteral("-movflags"), QStringLiteral("+faststart")};
    return p;
}

RenderProfile hevcProfile(const QString &uuid, const QString &name, const QString &category,
                          int width, int height, double fps, int bitrate, int quality = -1)
{
    RenderProfile p = base(uuid, name, category, QStringLiteral("libx265"), QStringLiteral("software"));
    p.fallbackVideoEncoder = QStringLiteral("libx265");
    p.width = width;
    p.height = height;
    p.frameRate = fps;
    p.videoBitrateKbps = bitrate;
    p.quality = quality;
    p.encoderPreset = QStringLiteral("medium");
    p.muxerArguments = {QStringLiteral("-tag:v"), QStringLiteral("hvc1"),
                        QStringLiteral("-movflags"), QStringLiteral("+faststart")};
    return p;
}

RenderProfile audioProfile(const QString &uuid, const QString &name, const QString &encoder,
                           const QString &extension, int bitrate)
{
    RenderProfile p = base(uuid, name, kAudio, QString(), QStringLiteral("software"), extension);
    p.videoEncoder.clear();
    p.pixelFormat.clear();
    p.encoderPreset.clear();
    p.quality = -1;
    p.audioEncoder = encoder;
    p.audioBitrateKbps = bitrate;
    p.allowSoftwareFallback = false;
    return p;
}
}

QList<ProfileCategory> ProfileCatalog::builtInCategories()
{
    return {
        {kBroadcast, {}, QStringLiteral("Broadcast"), true, 10},
        {kWeb, {}, QStringLiteral("Web and Social"), true, 20},
        {kEditing, {}, QStringLiteral("Editing and Mastering"), true, 30},
        {kProxy, {}, QStringLiteral("Proxy"), true, 35},
        {kArchive, {}, QStringLiteral("Archive"), true, 38},
        {kHardware, {}, QStringLiteral("Hardware Accelerated"), true, 40},
        {kNvidia, kHardware, QStringLiteral("NVIDIA NVENC / CUDA"), true, 41},
        {kIntel, kHardware, QStringLiteral("Intel Quick Sync"), true, 42},
        {kAmd, kHardware, QStringLiteral("AMD AMF"), true, 43},
        {kVaapi, kHardware, QStringLiteral("Linux VAAPI"), true, 44},
        {kAudio, {}, QStringLiteral("Audio Only"), true, 50},
        {kUser, {}, QStringLiteral("User Presets and Groups"), false, 100}
    };
}

QList<RenderProfile> ProfileCatalog::builtInProfiles(const FfmpegCapabilities &caps)
{
    QList<RenderProfile> out;

    // H.264 / MP4
    auto h264High = h264Profile(QStringLiteral("builtin-h264-match-high"),
                                QStringLiteral("Match Source — Adaptive High Bitrate"), kWeb,
                                0, 0, 0.0, 0, 18);
    h264High.description = QStringLiteral("Match-source H.264 with high quality CRF encoding and web fast-start.");
    addProfile(out, h264High, caps);

    auto h264Medium = h264Profile(QStringLiteral("builtin-h264-match-medium"),
                                  QStringLiteral("Match Source — Adaptive Medium Bitrate"), kWeb,
                                  0, 0, 0.0, 0, 21);
    h264Medium.encoderPreset = QStringLiteral("medium");
    addProfile(out, h264Medium, caps);

    addProfile(out, h264Profile(QStringLiteral("builtin-h264-4k-60"), QStringLiteral("4K UHD 2160p60 — 55 Mbps"), kWeb, 3840, 2160, 60.0, 55000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-4k-30"), QStringLiteral("4K UHD 2160p30 — 40 Mbps"), kWeb, 3840, 2160, 30.0, 40000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-1080-60"), QStringLiteral("Full HD 1080p60 — 16 Mbps"), kWeb, 1920, 1080, 60.0, 16000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-1080-30"), QStringLiteral("Full HD 1080p30 — 10 Mbps"), kWeb, 1920, 1080, 30.0, 10000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-720-30"), QStringLiteral("HD 720p30 — 5 Mbps"), kWeb, 1280, 720, 30.0, 5000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-social-vertical"), QStringLiteral("Vertical 1080x1920 — Social"), kWeb, 1080, 1920, 30.0, 10000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-square"), QStringLiteral("Square 1080x1080 — Social"), kWeb, 1080, 1080, 30.0, 8000), caps);

    addProfile(out, h264Profile(QStringLiteral("builtin-h264-broadcast-1080p25"), QStringLiteral("Broadcast H.264 1080p25 — 12 Mbps"), kBroadcast, 1920, 1080, 25.0, 12000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-h264-broadcast-1080p50"), QStringLiteral("Broadcast H.264 1080p50 — 20 Mbps"), kBroadcast, 1920, 1080, 50.0, 20000), caps);

    // HEVC / H.265
    auto hevcMatch = hevcProfile(QStringLiteral("builtin-hevc-match"), QStringLiteral("Match Source — High Quality"), kArchive, 0, 0, 0.0, 0, 22);
    addProfile(out, hevcMatch, caps);
    addProfile(out, hevcProfile(QStringLiteral("builtin-hevc-4k-60"), QStringLiteral("4K UHD 2160p60 — 35 Mbps"), kArchive, 3840, 2160, 60.0, 35000), caps);
    addProfile(out, hevcProfile(QStringLiteral("builtin-hevc-4k-30"), QStringLiteral("4K UHD 2160p30 — 25 Mbps"), kArchive, 3840, 2160, 30.0, 25000), caps);
    addProfile(out, hevcProfile(QStringLiteral("builtin-hevc-1080-50"), QStringLiteral("Full HD 1080p50 — 12 Mbps"), kArchive, 1920, 1080, 50.0, 12000), caps);
    addProfile(out, hevcProfile(QStringLiteral("builtin-hevc-1080-25"), QStringLiteral("Full HD 1080p25 — 8 Mbps"), kArchive, 1920, 1080, 25.0, 8000), caps);

    // AV1 and VP9
    RenderProfile av1 = base(QStringLiteral("builtin-av1-svt-match"), QStringLiteral("Match Source — SVT-AV1 High Quality"),
                             kArchive, QStringLiteral("libsvtav1"), QStringLiteral("software"), QStringLiteral("mkv"));
    av1.audioEncoder = QStringLiteral("libopus");
    av1.audioBitrateKbps = 192;
    av1.quality = 30;
    av1.encoderPreset = QStringLiteral("6");
    av1.fallbackVideoEncoder = QStringLiteral("libsvtav1");
    addProfile(out, av1, caps);

    RenderProfile vp9 = base(QStringLiteral("builtin-vp9-webm-1080"), QStringLiteral("WebM VP9 1080p"),
                             kWeb, QStringLiteral("libvpx-vp9"), QStringLiteral("software"), QStringLiteral("webm"));
    vp9.width = 1920; vp9.height = 1080; vp9.videoBitrateKbps = 6000; vp9.quality = -1;
    vp9.audioEncoder = QStringLiteral("libopus"); vp9.audioBitrateKbps = 160; vp9.encoderPreset.clear();
    addProfile(out, vp9, caps);

    // Apple ProRes
    for (const auto &[uuid, name, profileNumber, pixelFormat] :
         std::initializer_list<std::tuple<const char *, const char *, const char *, const char *>>{
             {"builtin-prores-proxy", "Apple ProRes 422 Proxy", "0", "yuv422p10le"},
             {"builtin-prores-lt", "Apple ProRes 422 LT", "1", "yuv422p10le"},
             {"builtin-prores-422", "Apple ProRes 422", "2", "yuv422p10le"},
             {"builtin-prores-hq", "Apple ProRes 422 HQ", "3", "yuv422p10le"},
             {"builtin-prores-4444", "Apple ProRes 4444", "4", "yuva444p10le"}}) {
        RenderProfile p = base(QString::fromLatin1(uuid), QString::fromLatin1(name),
                               QString::fromLatin1(profileNumber) == QStringLiteral("0") ? kProxy : kEditing,
                               QStringLiteral("prores_ks"), QStringLiteral("software"), QStringLiteral("mov"));
        p.audioEncoder = QStringLiteral("pcm_s24le");
        p.audioBitrateKbps = 0;
        p.pixelFormat = QString::fromLatin1(pixelFormat);
        p.encoderPreset.clear();
        p.quality = -1;
        p.videoArguments = {QStringLiteral("-profile:v"), QString::fromLatin1(profileNumber)};
        addProfile(out, p, caps);
    }

    // Avid DNxHR
    for (const auto &[uuid, name, profileName, pixelFormat] :
         std::initializer_list<std::tuple<const char *, const char *, const char *, const char *>>{
             {"builtin-dnxhr-lb", "Avid DNxHR LB", "dnxhr_lb", "yuv422p"},
             {"builtin-dnxhr-sq", "Avid DNxHR SQ", "dnxhr_sq", "yuv422p"},
             {"builtin-dnxhr-hq", "Avid DNxHR HQ", "dnxhr_hq", "yuv422p"},
             {"builtin-dnxhr-hqx", "Avid DNxHR HQX 10-bit", "dnxhr_hqx", "yuv422p10le"}}) {
        RenderProfile p = base(QString::fromLatin1(uuid), QString::fromLatin1(name), kEditing,
                               QStringLiteral("dnxhd"), QStringLiteral("software"), QStringLiteral("mov"));
        p.audioEncoder = QStringLiteral("pcm_s24le");
        p.audioBitrateKbps = 0;
        p.pixelFormat = QString::fromLatin1(pixelFormat);
        p.encoderPreset.clear();
        p.quality = -1;
        p.videoArguments = {QStringLiteral("-profile:v"), QString::fromLatin1(profileName)};
        addProfile(out, p, caps);
    }

    // MPEG-2 broadcast/interchange
    RenderProfile mpeg2 = base(QStringLiteral("builtin-mpeg2-broadcast-50"),
                               QStringLiteral("MPEG-2 4:2:2 1080i50 — 50 Mbps"), kBroadcast,
                               QStringLiteral("mpeg2video"), QStringLiteral("software"), QStringLiteral("mxf"));
    mpeg2.width = 1920; mpeg2.height = 1080; mpeg2.frameRate = 25.0; mpeg2.videoBitrateKbps = 50000;
    mpeg2.quality = -1; mpeg2.pixelFormat = QStringLiteral("yuv422p"); mpeg2.encoderPreset.clear();
    mpeg2.audioEncoder = QStringLiteral("pcm_s24le"); mpeg2.audioBitrateKbps = 0;
    mpeg2.videoArguments = {QStringLiteral("-minrate"), QStringLiteral("50000k"),
                            QStringLiteral("-maxrate"), QStringLiteral("50000k"),
                            QStringLiteral("-bufsize"), QStringLiteral("17825792")};
    addProfile(out, mpeg2, caps);

    // Lightweight proxies
    addProfile(out, h264Profile(QStringLiteral("builtin-proxy-h264-720"), QStringLiteral("H.264 Proxy 720p — 2 Mbps"), kProxy, 1280, 720, 25.0, 2000), caps);
    addProfile(out, h264Profile(QStringLiteral("builtin-proxy-h264-540"), QStringLiteral("H.264 Proxy 960x540 — 1 Mbps"), kProxy, 960, 540, 25.0, 1000), caps);

    // Audio-only
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-aac-320"), QStringLiteral("AAC 320 kbps"), QStringLiteral("aac"), QStringLiteral("m4a"), 320), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-aac-192"), QStringLiteral("AAC 192 kbps"), QStringLiteral("aac"), QStringLiteral("m4a"), 192), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-mp3-320"), QStringLiteral("MP3 320 kbps"), QStringLiteral("libmp3lame"), QStringLiteral("mp3"), 320), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-mp3-192"), QStringLiteral("MP3 192 kbps"), QStringLiteral("libmp3lame"), QStringLiteral("mp3"), 192), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-wav-24"), QStringLiteral("Waveform Audio 24-bit PCM"), QStringLiteral("pcm_s24le"), QStringLiteral("wav"), 0), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-wav-16"), QStringLiteral("Waveform Audio 16-bit PCM"), QStringLiteral("pcm_s16le"), QStringLiteral("wav"), 0), caps);
    addProfile(out, audioProfile(QStringLiteral("builtin-audio-flac"), QStringLiteral("FLAC Lossless"), QStringLiteral("flac"), QStringLiteral("flac"), 0), caps);

    // Hardware accelerated families
    for (const auto &[uuid, name, encoder, quality, category, backend] :
         std::initializer_list<std::tuple<const char *, const char *, const char *, int, const QString *, const char *>>{
             {"builtin-nvenc-h264", "NVIDIA H.264 NVENC — Match Source", "h264_nvenc", 19, &kNvidia, "nvenc"},
             {"builtin-nvenc-hevc", "NVIDIA HEVC NVENC — Match Source", "hevc_nvenc", 21, &kNvidia, "nvenc"},
             {"builtin-nvenc-av1", "NVIDIA AV1 NVENC — Match Source", "av1_nvenc", 24, &kNvidia, "nvenc"},
             {"builtin-qsv-h264", "Intel H.264 Quick Sync — Match Source", "h264_qsv", 21, &kIntel, "qsv"},
             {"builtin-qsv-hevc", "Intel HEVC Quick Sync — Match Source", "hevc_qsv", 23, &kIntel, "qsv"},
             {"builtin-qsv-av1", "Intel AV1 Quick Sync — Match Source", "av1_qsv", 25, &kIntel, "qsv"},
             {"builtin-amf-h264", "AMD H.264 AMF — Match Source", "h264_amf", 21, &kAmd, "amf"},
             {"builtin-amf-hevc", "AMD HEVC AMF — Match Source", "hevc_amf", 23, &kAmd, "amf"},
             {"builtin-amf-av1", "AMD AV1 AMF — Match Source", "av1_amf", 25, &kAmd, "amf"},
             {"builtin-vaapi-h264", "VAAPI H.264 — Match Source", "h264_vaapi", 21, &kVaapi, "vaapi"},
             {"builtin-vaapi-hevc", "VAAPI HEVC — Match Source", "hevc_vaapi", 23, &kVaapi, "vaapi"},
             {"builtin-vaapi-av1", "VAAPI AV1 — Match Source", "av1_vaapi", 25, &kVaapi, "vaapi"}}) {
        RenderProfile p = base(QString::fromLatin1(uuid), QString::fromLatin1(name), *category,
                               QString::fromLatin1(encoder), QString::fromLatin1(backend));
        p.encoderPreset = QString::fromLatin1(backend) == QStringLiteral("nvenc") ? QStringLiteral("p5") : QStringLiteral("medium");
        if (QString::fromLatin1(backend) == QStringLiteral("amf")) p.encoderPreset = QStringLiteral("balanced");
        if (QString::fromLatin1(backend) == QStringLiteral("vaapi")) p.encoderPreset.clear();
        p.quality = quality;
        if (p.videoEncoder.contains(QStringLiteral("hevc"))) p.fallbackVideoEncoder = QStringLiteral("libx265");
        else if (p.videoEncoder.contains(QStringLiteral("av1"))) p.fallbackVideoEncoder = QStringLiteral("libsvtav1");
        addProfile(out, p, caps);
    }

    RenderProfile cudaDecode = base(QStringLiteral("builtin-nvenc-h264-cuda"),
                                    QStringLiteral("NVIDIA H.264 NVENC + CUDA Decode"),
                                    kNvidia, QStringLiteral("h264_nvenc"), QStringLiteral("nvenc"));
    cudaDecode.description = QStringLiteral("CUDA hardware decoding with NVENC encoding when supported by the source codec.");
    cudaDecode.encoderPreset = QStringLiteral("p5");
    cudaDecode.quality = 19;
    cudaDecode.inputArguments = {QStringLiteral("-hwaccel"), QStringLiteral("cuda"),
                                 QStringLiteral("-hwaccel_output_format"), QStringLiteral("cuda")};
    applyAvailability(cudaDecode, caps);
    if (cudaDecode.enabled && !caps.hwaccels.contains(QStringLiteral("cuda"))) {
        cudaDecode.enabled = false;
        cudaDecode.unavailableReason = QStringLiteral("CUDA hardware acceleration is not enabled in this FFmpeg build");
    }
    out.push_back(cudaDecode);

    return out;
}

void ProfileCatalog::seedMissing(Database &database, const FfmpegCapabilities &capabilities)
{
    const auto existingCategories = database.loadCategories();
    QSet<QString> categoryIds;
    for (const auto &category : existingCategories) categoryIds.insert(category.uuid);
    for (const auto &category : builtInCategories())
        if (!categoryIds.contains(category.uuid)) database.saveCategory(category);

    const auto existingProfiles = database.loadProfiles();
    QMap<QString, RenderProfile> profileMap;
    for (const auto &profile : existingProfiles) profileMap.insert(profile.uuid, profile);
    for (const auto &profile : builtInProfiles(capabilities)) {
        if (!profileMap.contains(profile.uuid) || profileMap.value(profile.uuid).builtIn)
            database.saveProfile(profile);
    }
}

} // namespace flux
