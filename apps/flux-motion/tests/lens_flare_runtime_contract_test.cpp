#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}
void reject(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "unexpected: " << needle << '\n';
        assert(false);
    }
}
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    const std::string registry = read_file(argv[1]);
    const std::string title_source = read_file(argv[2]);
    const std::string cache_manager = read_file(argv[3]);
    const std::string shader = read_file(argv[4]);

    require(registry, "kEmbeddedLensFlareEffect");
    require(registry, "uniform float ghostCount;");
    require(registry, "uniform float profile;");
    require(registry, "float flare_disc(float2 flare_pos, float discRadius, float feather)");
    reject(registry, "float2 point");
    reject(registry, "float disc_radius");
    reject(registry, "float2 line");
    reject(registry, "float line");
    require(registry, "float flare_halo");
    require(registry, "float output_alpha = saturate(base.a + flare_alpha * (1.0 - base.a));");
    require(registry, "float3 output_rgb = saturate(base.rgb + flare_color * flare_alpha);");
    require(registry, "step(6.5, ghostCount)");
    require(registry, "anamorphic_profile");
    require(registry, "warm_profile");
    require(registry, "scifi_profile");
    require(registry, "subtle_profile");

    require(title_source, "resolved.type == LayerEffectType::LensFlare");
    require(title_source, "set_effect_float_param(effect, \"ghostCount\"");
    require(title_source, "set_effect_float_param(effect, \"profile\"");
    require(title_source, "gpu-effects-v8-lens-flare-dx11-keyword-fix");
    require(title_source, "Lens flare GPU pass active passes=%1");
    require(title_source, "Lens flare Draw technique executed no passes");
    require(cache_manager, "gpu-renderer-v31-lens-flare-dx11-keyword-fix");

    require(shader, "uniform float ghostCount;");
    require(shader, "uniform float profile;");
    require(shader, "float flare_disc(float2 flare_pos, float discRadius, float feather)");
    reject(shader, "float2 point");
    reject(shader, "float disc_radius");
    reject(shader, "float2 line");
    reject(shader, "float line");
    require(shader, "technique Draw");
    require(shader, "pixel_shader = PSLensFlare(v_in);");
    reject(shader, "uniform float samples;");
    reject(shader, "for (");
    reject(shader, "while (");
    reject(shader, "ddx(");
    reject(shader, "ddy(");
    reject(shader, "discard;");

    std::cout << "lens flare runtime contract: PASS\n";
    return 0;
}
