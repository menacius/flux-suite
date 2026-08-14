#pragma once

#include <stdint.h>

#ifdef _WIN32
#define FXM_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FXM_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define FXM_PLUGIN_API_VERSION_1 1u
#define FXM_PLUGIN_API_VERSION_2 2u
#define FXM_PLUGIN_API_VERSION_3 3u
#define FXM_PLUGIN_API_VERSION_4 4u
#define FXM_PLUGIN_API_VERSION FXM_PLUGIN_API_VERSION_4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fxm_host_api_v1 {
    uint32_t api_version;
    void (*log)(int level, const char *component, const char *message);
} fxm_host_api_v1;

typedef struct fxm_effect_descriptor_v1 {
    const char *id;
    const char *display_name;
    const char *category;
    const char *shader_path;
    const char *manifest_json;
} fxm_effect_descriptor_v1;

typedef struct fxm_plugin_descriptor_v1 {
    uint32_t api_version;
    const char *id;
    const char *name;
    const char *version;
    uint32_t effect_count;
    const fxm_effect_descriptor_v1 *effects;
} fxm_plugin_descriptor_v1;

/* ABI v2 remains pure C and append-only. The host owns every editor widget.
 * Plugins describe compound editors, presets and asset packs as UTF-8 JSON.
 * Optional callbacks validate/migrate opaque project state without exposing Qt. */
typedef int (*fxm_validate_state_v2_fn)(const char *effect_id, const char *state_json,
                                        char *error_utf8, uint32_t error_capacity);
typedef const char *(*fxm_migrate_state_v2_fn)(const char *effect_id,
                                               uint32_t from_schema_version,
                                               const char *state_json);
typedef void (*fxm_release_string_v2_fn)(const char *value);

typedef struct fxm_effect_descriptor_v2 {
    fxm_effect_descriptor_v1 v1;
    uint32_t schema_version;
    const char *editor_schema_json; /* declarative host-owned editor */
    const char *preset_index_json;  /* categories + relative preset files */
    const char *asset_index_json;   /* textures/LUTs/icons + metadata */
    const char *capabilities_json;  /* compoundGraph, customAssets, keyframes, etc. */
    const char *animation_schema_json; /* animatable paths, interpolation/easing policy */
} fxm_effect_descriptor_v2;

/* ABI v3 adds host-rendered, host-hit-tested canvas controls. The JSON schema
 * describes handles by parameter path and coordinate space; plugins never
 * receive QWidget/QPainter pointers and remain ABI-stable across Qt versions. */
typedef struct fxm_effect_descriptor_v3 {
    fxm_effect_descriptor_v2 v2;
    const char *canvas_handles_schema_json;
} fxm_effect_descriptor_v3;

/* ABI v4 is the public Visual Effects SDK contract. It remains pure C and
 * append-only. GPU effects are described by shader passes and metadata; CPU
 * effects may only be scheduled through the host worker pipeline declared in
 * requirements_json/capabilities_json and must never execute on the FXM render
 * loop. Custom property widgets are declarative JSON widgets owned by FXM. */
typedef enum fxm_effect_color_space_v4 {
    FXM_COLOR_SPACE_PRESERVE_INPUT = 0,
    FXM_COLOR_SPACE_SCENE_LINEAR = 1,
    FXM_COLOR_SPACE_DISPLAY_REFERRED = 2,
    FXM_COLOR_SPACE_HDR_LINEAR = 3
} fxm_effect_color_space_v4;

typedef enum fxm_effect_alpha_contract_v4 {
    FXM_ALPHA_PREMULTIPLIED_PRESERVE = 0,
    FXM_ALPHA_PREMULTIPLIED_EXPAND = 1,
    FXM_ALPHA_PREMULTIPLIED_REPLACE = 2,
    FXM_ALPHA_STRAIGHT_INPUT_REQUIRED = 3
} fxm_effect_alpha_contract_v4;

typedef enum fxm_effect_backend_v4 {
    FXM_EFFECT_BACKEND_GPU_SHADER = 0,
    FXM_EFFECT_BACKEND_GPU_MULTI_PASS = 1,
    FXM_EFFECT_BACKEND_CPU_WORKER_ONLY = 2,
    FXM_EFFECT_BACKEND_HYBRID_WORKER_AND_GPU = 3
} fxm_effect_backend_v4;

typedef struct fxm_effect_descriptor_v4 {
    fxm_effect_descriptor_v3 v3;
    uint32_t descriptor_size;
    uint32_t input_count;               /* main input + declared aux inputs */
    fxm_effect_backend_v4 backend;
    fxm_effect_color_space_v4 color_space;
    fxm_effect_alpha_contract_v4 alpha_contract;
    const char *parameter_metadata_json; /* stable parameter ids, ranges, units, defaults */
    const char *custom_property_widgets_json; /* host-owned custom editors */
    const char *render_passes_json;      /* ordered GPU shader passes/targets */
    const char *inputs_json;             /* primary + auxiliary input declarations */
    const char *auxiliary_inputs_json;   /* named auxiliary composition/layer inputs */
    const char *layer_references_json;   /* explicit layer-reference policy */
    const char *requirements_json;       /* color/alpha/bounds/HDR/cache/thread requirements */
    const char *state_serialization_json;/* state format, defaults, missing-plugin fallback */
} fxm_effect_descriptor_v4;

typedef int (*fxm_plugin_can_unload_v4_fn)(const char *plugin_id);
typedef void (*fxm_plugin_before_unload_v4_fn)(const char *plugin_id);

typedef struct fxm_plugin_descriptor_v2 {
    fxm_plugin_descriptor_v1 v1;
    uint32_t descriptor_size;
    uint32_t effect_v2_count;
    const fxm_effect_descriptor_v2 *effects_v2;
    fxm_validate_state_v2_fn validate_state;
    fxm_migrate_state_v2_fn migrate_state;
    fxm_release_string_v2_fn release_string;
} fxm_plugin_descriptor_v2;

typedef struct fxm_plugin_descriptor_v3 {
    fxm_plugin_descriptor_v2 v2;
    uint32_t effect_v3_count;
    const fxm_effect_descriptor_v3 *effects_v3;
} fxm_plugin_descriptor_v3;

typedef struct fxm_plugin_descriptor_v4 {
    fxm_plugin_descriptor_v3 v3;
    uint32_t descriptor_size;
    uint32_t effect_v4_count;
    const fxm_effect_descriptor_v4 *effects_v4;
    fxm_plugin_can_unload_v4_fn can_unload;
    fxm_plugin_before_unload_v4_fn before_unload;
} fxm_plugin_descriptor_v4;

typedef const fxm_plugin_descriptor_v1 *(*fxm_plugin_query_v1_fn)(const fxm_host_api_v1 *host);
typedef const fxm_plugin_descriptor_v2 *(*fxm_plugin_query_v2_fn)(const fxm_host_api_v1 *host);
typedef const fxm_plugin_descriptor_v3 *(*fxm_plugin_query_v3_fn)(const fxm_host_api_v1 *host);
typedef const fxm_plugin_descriptor_v4 *(*fxm_plugin_query_v4_fn)(const fxm_host_api_v1 *host);

FXM_PLUGIN_EXPORT const fxm_plugin_descriptor_v1 *fxm_plugin_query_v1(const fxm_host_api_v1 *host);
FXM_PLUGIN_EXPORT const fxm_plugin_descriptor_v2 *fxm_plugin_query_v2(const fxm_host_api_v1 *host);
FXM_PLUGIN_EXPORT const fxm_plugin_descriptor_v3 *fxm_plugin_query_v3(const fxm_host_api_v1 *host);
FXM_PLUGIN_EXPORT const fxm_plugin_descriptor_v4 *fxm_plugin_query_v4(const fxm_host_api_v1 *host);

#ifdef __cplusplus
}
#endif
