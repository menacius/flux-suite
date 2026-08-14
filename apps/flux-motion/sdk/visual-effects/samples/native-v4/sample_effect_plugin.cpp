#include "../../../../src/extensions/fxm-plugin-api.h"

static const char kParameters[] = R"json({
  "parameters": {
    "amount": { "type": "float", "label": "Amount", "min": 0, "max": 1, "step": 0.01, "default": 1, "animatable": true }
  },
  "defaults": { "amount": 1.0 }
})json";

static const char kRenderPasses[] = R"json([
  { "name": "main", "shader": "sample.effect", "technique": "Draw" }
])json";

static const char kInputs[] = R"json([
  { "id": "source", "label": "Layer Source", "kind": "current-layer", "required": true }
])json";

static const char kRequirements[] = R"json({
  "colorSpace": "preserve-input",
  "alpha": "premultiplied-preserve",
  "cacheableWhenStatic": true
})json";

static const fxm_effect_descriptor_v4 kEffects[] = {
    {
        {
            {
                {
                    "com.example.native.sample.v1",
                    "Native SDK Sample",
                    "SDK Samples",
                    "sample.effect",
                    kParameters
                },
                1,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            },
            nullptr
        },
        sizeof(fxm_effect_descriptor_v4),
        1,
        FXM_EFFECT_BACKEND_GPU_SHADER,
        FXM_COLOR_SPACE_PRESERVE_INPUT,
        FXM_ALPHA_PREMULTIPLIED_PRESERVE,
        kParameters,
        nullptr,
        kRenderPasses,
        kInputs,
        nullptr,
        nullptr,
        kRequirements,
        nullptr
    }
};

extern "C" FXM_PLUGIN_EXPORT const fxm_plugin_descriptor_v4 *fxm_plugin_query_v4(const fxm_host_api_v1 *host)
{
    static const fxm_plugin_descriptor_v4 descriptor = {
        {
            {
                {
                    FXM_PLUGIN_API_VERSION_4,
                    "com.example.native",
                    "Native SDK Sample Plugin",
                    "1.0.0",
                    0,
                    nullptr
                },
                sizeof(fxm_plugin_descriptor_v2),
                0,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            },
            0,
            nullptr
        },
        sizeof(fxm_plugin_descriptor_v4),
        1,
        kEffects,
        nullptr,
        nullptr
    };
    if (host && host->log)
        host->log(1, "com.example.native", "Native SDK sample queried");
    return &descriptor;
}
