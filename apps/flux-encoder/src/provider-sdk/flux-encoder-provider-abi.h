#pragma once
#include "flux-encoder-provider-version.h"
#include "flux-encoder-provider-types.h"
#if defined(_WIN32)
#  if defined(FLUX_ENCODER_PROVIDER_BUILD)
#    define FLUX_ENCODER_PROVIDER_EXPORT __declspec(dllexport)
#  else
#    define FLUX_ENCODER_PROVIDER_EXPORT __declspec(dllimport)
#  endif
#else
#  define FLUX_ENCODER_PROVIDER_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
FLUX_ENCODER_PROVIDER_EXPORT uint32_t flux_encoder_provider_get_abi_version(void);
FLUX_ENCODER_PROVIDER_EXPORT FluxEncoderProviderResult flux_encoder_provider_create(const FluxEncoderProviderHostApi *host, FluxEncoderProviderHandle **provider);
FLUX_ENCODER_PROVIDER_EXPORT void flux_encoder_provider_destroy(FluxEncoderProviderHandle *provider);
FLUX_ENCODER_PROVIDER_EXPORT FluxEncoderProviderResult flux_encoder_provider_get_info(FluxEncoderProviderHandle *provider, FluxEncoderProviderInfo *info);
#ifdef __cplusplus
}
#endif
