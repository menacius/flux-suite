#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxEncoderProviderHandle FluxEncoderProviderHandle;
typedef struct FluxEncoderRenderSession FluxEncoderRenderSession;

typedef enum FluxEncoderProviderResult {
    FLUX_ENCODER_PROVIDER_OK = 0,
    FLUX_ENCODER_PROVIDER_ERROR = 1,
    FLUX_ENCODER_PROVIDER_UNSUPPORTED = 2,
    FLUX_ENCODER_PROVIDER_CANCELLED = 3,
    FLUX_ENCODER_PROVIDER_INVALID_ARGUMENT = 4,
    FLUX_ENCODER_PROVIDER_VERSION_MISMATCH = 5
} FluxEncoderProviderResult;

typedef enum FluxEncoderRenderCapability {
    FLUX_ENCODER_CAP_PROJECT_PROBE = 1ull << 0,
    FLUX_ENCODER_CAP_VIDEO_FRAMES = 1ull << 1,
    FLUX_ENCODER_CAP_AUDIO_FRAMES = 1ull << 2,
    FLUX_ENCODER_CAP_ALPHA = 1ull << 3,
    FLUX_ENCODER_CAP_HDR = 1ull << 4,
    FLUX_ENCODER_CAP_FRAME_RANGES = 1ull << 5,
    FLUX_ENCODER_CAP_EXTERNAL_DATA_SNAPSHOT = 1ull << 6,
    FLUX_ENCODER_CAP_GPU_SHARED_TEXTURE = 1ull << 7,
    FLUX_ENCODER_CAP_RESUME = 1ull << 8
} FluxEncoderRenderCapability;

typedef struct FluxEncoderProviderInfo {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *provider_id;
    const char *display_name;
    const char *provider_version;
    const char *vendor;
    uint64_t capabilities;
    uint64_t reserved[8];
} FluxEncoderProviderInfo;

typedef struct FluxEncoderVideoFrame {
    uint32_t struct_size;
    int64_t pts_ns;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t plane_count;
    const uint8_t *planes[4];
    uint32_t plane_strides[4];
    uint32_t color_primaries;
    uint32_t transfer_characteristics;
    uint32_t matrix_coefficients;
    uint32_t color_range;
    uint64_t reserved[8];
} FluxEncoderVideoFrame;

typedef struct FluxEncoderAudioBlock {
    uint32_t struct_size;
    int64_t pts_ns;
    uint32_t sample_rate;
    uint32_t channel_count;
    uint32_t sample_format;
    uint32_t frame_count;
    const void *planes[32];
    uint64_t reserved[8];
} FluxEncoderAudioBlock;

typedef void (*FluxEncoderLogCallback)(void *user_data, int level, const char *utf8_message);
typedef struct FluxEncoderProviderHostApi {
    uint32_t struct_size;
    uint32_t abi_version;
    void *user_data;
    FluxEncoderLogCallback log;
    void *reserved[12];
} FluxEncoderProviderHostApi;

#ifdef __cplusplus
}
#endif
