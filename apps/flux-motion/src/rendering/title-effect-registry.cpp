#include "title-effect-registry.h"
#include "extensions/effect-extension-catalog.h"

#include "title-logger.h"
#include "performance-counters.h"
#include "asset-path-provider.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>

#include <QString>

#include <algorithm>

namespace {

static constexpr const char *kEmbeddedLensFlareEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 texelSize;
uniform float4 effectColor;
uniform float4 secondaryColor;
uniform float opacity;
uniform float amount;
uniform float scale;
uniform float radius;
uniform float spread;
uniform float falloff;
uniform float2 center;
uniform float angle;
uniform float ghostCount;
uniform float profile;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertDataOut {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv = v_in.uv;
    return vert_out;
}

float flare_disc(float2 flare_pos, float discRadius, float feather)
{
    float safe_radius = max(discRadius, 0.0001);
    float safe_feather = max(feather, 0.0001);
    return 1.0 - smoothstep(safe_radius - safe_feather,
                            safe_radius + safe_feather,
                            length(flare_pos));
}

float flare_halo(float distance_value, float halo_radius, float decay)
{
    float normalized = distance_value / max(halo_radius, 0.0001);
    return 1.0 / (1.0 + normalized * normalized * max(decay, 0.05));
}

float4 PSLensFlare(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);

    /* Keep the procedural geometry in square pixel space.  This avoids the
     * DX11/OpenGL divergence caused by deriving the flare shape directly from
     * non-square UV coordinates. */
    float aspect = max(texelSize.y / max(texelSize.x, 0.000001), 0.0001);
    float2 flare_pos = v_in.uv - center;
    flare_pos.x *= aspect;

    float safe_scale = max(scale, 0.001);
    float safe_radius = max(radius * safe_scale, 0.001);
    float distance_value = length(flare_pos);
    float safe_falloff = max(falloff, 0.05);

    float core = flare_disc(flare_pos, safe_radius * 0.38,
                            safe_radius * 0.16);
    float glow = flare_halo(distance_value, safe_radius * 1.55,
                            safe_falloff * 2.0);
    float outer_halo = flare_disc(flare_pos, safe_radius * 2.35,
                                  safe_radius * 0.28) * 0.22;

    float radians = angle * 0.01745329252;
    float2 axis = float2(cos(radians), sin(radians));
    float2 perpendicular = float2(-axis.y, axis.x);
    float along_axis = abs(dot(flare_pos, axis));
    float across_axis = abs(dot(flare_pos, perpendicular));
    float ray_width = safe_radius * 0.055;
    float ray_length = safe_radius * 8.0;
    float primary_ray = (1.0 - smoothstep(0.0, ray_width, across_axis)) *
                        (1.0 - smoothstep(safe_radius * 0.25,
                                          ray_length, along_axis));
    float secondary_ray = (1.0 - smoothstep(0.0, ray_width * 0.65,
                                             along_axis)) *
                          (1.0 - smoothstep(safe_radius * 0.25,
                                            ray_length * 0.72,
                                            across_axis));

    /* Ghosts lie on the optical axis between the flare and the frame centre.
     * They are written explicitly rather than through a dynamic loop because
     * OBS must compile the same effect for D3D11 and OpenGL. */
    float2 optical_axis = float2(0.5, 0.5) - center;
    optical_axis.x *= aspect;
    float safe_spread = max(spread, 0.0);
    float ghost1 = flare_disc(flare_pos - optical_axis * (0.55 + safe_spread * 0.30),
                              safe_radius * 0.42, safe_radius * 0.16);
    float ghost2 = flare_disc(flare_pos - optical_axis * (1.15 + safe_spread * 0.60),
                              safe_radius * 0.28, safe_radius * 0.12) *
                   step(2.5, ghostCount);
    float ghost3 = flare_disc(flare_pos - optical_axis * (1.85 + safe_spread * 0.95),
                              safe_radius * 0.36, safe_radius * 0.14) *
                   step(4.5, ghostCount);
    float ghost4 = flare_disc(flare_pos + optical_axis * (0.42 + safe_spread * 0.20),
                              safe_radius * 0.20, safe_radius * 0.10) *
                   step(6.5, ghostCount);

    float anamorphic = (1.0 - smoothstep(0.0, safe_radius * 0.045,
                                         abs(flare_pos.y))) *
                        (1.0 - smoothstep(safe_radius * 0.35,
                                          safe_radius * 11.0,
                                          abs(flare_pos.x)));

    float profile_value = profile;
    float anamorphic_profile = 1.0 - step(0.5, abs(profile_value - 1.0));
    float warm_profile = 1.0 - step(0.5, abs(profile_value - 2.0));
    float scifi_profile = 1.0 - step(0.5, abs(profile_value - 3.0));
    float subtle_profile = 1.0 - step(0.5, abs(profile_value - 4.0));

    float flare_shape = core * 1.35 + glow * 0.58 + outer_halo +
                        primary_ray * (0.34 + scifi_profile * 0.30) +
                        secondary_ray * 0.18 +
                        ghost1 * 0.42 + ghost2 * 0.30 +
                        ghost3 * 0.24 + ghost4 * 0.18 +
                        anamorphic * (anamorphic_profile * 0.85 +
                                      scifi_profile * 0.55);
    flare_shape *= lerp(1.0, 0.48, subtle_profile);

    float color_mix = saturate(distance_value /
                               max(safe_radius * 4.0, 0.001));
    float3 flare_color = lerp(effectColor.rgb,
                              secondaryColor.rgb,
                              color_mix);
    flare_color = lerp(flare_color,
                       float3(1.0, 0.48, 0.16),
                       warm_profile * 0.38);

    float alpha_scale = max(effectColor.a, secondaryColor.a);
    float flare_alpha = saturate(flare_shape * max(amount, 0.0) *
                                 clamp(opacity, 0.0, 1.0) * alpha_scale);

    /* OBS textures use premultiplied alpha.  The flare is generative, so it
     * must be able to create visible pixels even when the input is transparent
     * while still preserving the source alpha everywhere else. */
    float output_alpha = saturate(base.a + flare_alpha * (1.0 - base.a));
    float3 output_rgb = saturate(base.rgb + flare_color * flare_alpha);
    return float4(output_rgb, output_alpha);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSLensFlare(v_in);
    }
}
)FXMFX";
static constexpr const char *kEmbeddedDetailEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d blurredImage;
uniform float2 texelSize;
uniform float opacity;
uniform float amount;
uniform float threshold;
uniform float highlightProtection;
uniform float shadowProtection;
uniform float midtoneBias;
uniform float rangeThreshold;
uniform float edgeProtection;
uniform int luminanceOnly;
uniform int overlayPreview;
uniform int protectAlpha;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut o;
    o.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    o.uv = v_in.uv;
    return o;
}
float detail_luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}
float3 detail_straight(float4 c)
{
    return c.a > 0.00001 ? c.rgb / c.a : float3(0.0, 0.0, 0.0);
}
float detail_gate(float3 detail)
{
    float magnitude = abs(detail_luma(detail));
    return smoothstep(max(threshold, 0.0), max(threshold, 0.0) + 0.02, magnitude);
}
float detail_tone_weight(float luminance)
{
    float shadow = lerp(1.0, smoothstep(0.0, 0.35, luminance), saturate(shadowProtection));
    float highlight = lerp(1.0, 1.0 - smoothstep(0.65, 1.0, luminance), saturate(highlightProtection));
    return shadow * highlight;
}
float3 detail_vector(float3 source, float3 lowpass)
{
    float3 d = source - lowpass;
    if (luminanceOnly != 0) {
        float y = detail_luma(d);
        d = float3(y, y, y);
    }
    return d;
}
float4 detail_output(float4 base, float3 result, float alpha_delta)
{
    float out_alpha = base.a;
    if (protectAlpha == 0)
        out_alpha = saturate(base.a + alpha_delta);
    return float4(saturate(result) * out_alpha, out_alpha);
}
float3 overlay_blend(float3 a, float3 b)
{
    float3 low = 2.0 * a * b;
    float3 high = 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    return lerp(low, high, step(float3(0.5, 0.5, 0.5), a));
}
float4 PSSharpen(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float3 source = detail_straight(base);
    float3 lowpass = detail_straight(blurredImage.Sample(textureSampler, v_in.uv));
    float3 d = detail_vector(source, lowpass);
    float gate = detail_gate(d) * detail_tone_weight(detail_luma(source));
    float3 result = source + d * amount * gate;
    result = lerp(source, result, saturate(opacity));
    return detail_output(base, result, detail_luma(d) * amount * 0.04);
}
float4 PSUnsharpMask(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float3 source = detail_straight(base);
    float3 lowpass = detail_straight(blurredImage.Sample(textureSampler, v_in.uv));
    float3 d = detail_vector(source, lowpass);
    float gate = detail_gate(d) * detail_tone_weight(detail_luma(source));
    float3 result = source + d * amount * gate;
    result = lerp(source, result, saturate(opacity));
    return detail_output(base, result, detail_luma(d) * amount * 0.04);
}
float4 PSHighPass(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float3 source = detail_straight(base);
    float3 lowpass = detail_straight(blurredImage.Sample(textureSampler, v_in.uv));
    float3 d = detail_vector(source, lowpass);
    float3 highpass = saturate(float3(0.5, 0.5, 0.5) + d * amount);
    float3 result = overlayPreview != 0 ? overlay_blend(source, highpass) : highpass;
    result = lerp(source, result, saturate(opacity));
    return detail_output(base, result, 0.0);
}
float4 PSClarity(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float3 source = detail_straight(base);
    float3 lowpass = detail_straight(blurredImage.Sample(textureSampler, v_in.uv));
    float3 d = detail_vector(source, lowpass);
    float luminance = detail_luma(source);
    float center = 0.5 + clamp(midtoneBias, -1.0, 1.0) * 0.25;
    float midtone = 1.0 - saturate(abs(luminance - center) / 0.5);
    float gate = detail_gate(d) * detail_tone_weight(luminance);
    float3 result = source + d * amount * midtone * gate;
    result = lerp(source, result, saturate(opacity));
    return detail_output(base, result, detail_luma(d) * amount * 0.03);
}
float4 PSBilateralSharpen(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float3 source = detail_straight(base);
    float3 lowpass = detail_straight(blurredImage.Sample(textureSampler, v_in.uv));
    float difference = abs(detail_luma(source) - detail_luma(lowpass));
    float edge_gate = 1.0 - smoothstep(max(rangeThreshold, 0.001),
                                       max(rangeThreshold, 0.001) * 2.0,
                                       difference);
    edge_gate = lerp(1.0, edge_gate, saturate(edgeProtection));
    float3 d = detail_vector(source, lowpass);
    float gate = detail_gate(d) * edge_gate;
    float3 result = source + d * amount * gate;
    result = lerp(source, result, saturate(opacity));
    return detail_output(base, result, detail_luma(d) * amount * 0.025);
}
technique Sharpen { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSSharpen(v_in); } }
technique UnsharpMask { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSUnsharpMask(v_in); } }
technique HighPass { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSHighPass(v_in); } }
technique Clarity { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSClarity(v_in); } }
technique BilateralSharpen { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSBilateralSharpen(v_in); } }
technique Draw { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSSharpen(v_in); } }
)FXMFX";
static constexpr const char *kEmbeddedGlareEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d blurredImage;
uniform float2 texelSize;
uniform float4 effectColor;
uniform float4 secondaryColor;
uniform float opacity;
uniform float amount;
uniform float radius;
uniform float shadowLength;
uniform float angle;
uniform float softness;
uniform float intensity;

sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v_in) { VertDataOut o; o.pos = mul(float4(v_in.pos.xyz,1.0), ViewProj); o.uv=v_in.uv; return o; }
float4 glare_sample(float2 uv) { return blurredImage.Sample(textureSampler, uv); }
float4 PSGlare(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float radians = angle * 0.01745329252;
    float2 axis = float2(cos(radians), sin(radians));
    float2 perpendicular = float2(-axis.y, axis.x);
    float length_pixels = max(shadowLength, max(radius, 1.0));
    float2 d = axis * texelSize * length_pixels * 0.125;
    float2 p = perpendicular * texelSize * max(radius, 1.0) * 0.18;
    float4 streak = glare_sample(v_in.uv) * 0.22;
    streak += (glare_sample(v_in.uv + d) + glare_sample(v_in.uv - d)) * 0.14;
    streak += (glare_sample(v_in.uv + d * 2.0) + glare_sample(v_in.uv - d * 2.0)) * 0.10;
    streak += (glare_sample(v_in.uv + d * 4.0) + glare_sample(v_in.uv - d * 4.0)) * 0.065;
    streak += (glare_sample(v_in.uv + d * 8.0) + glare_sample(v_in.uv - d * 8.0)) * 0.035;
    float4 cross_streak = (glare_sample(v_in.uv + p) + glare_sample(v_in.uv - p)) * 0.10;
    float chroma = saturate(softness);
    float3 optical = streak.rgb * effectColor.rgb;
    optical += cross_streak.rgb * secondaryColor.rgb * (0.35 + chroma * 0.65);
    float gain = max(amount, 0.0) * max(intensity, 0.0) * clamp(opacity, 0.0, 1.0);
    float3 output_rgb = base.rgb + optical * gain;
    float output_alpha = saturate(base.a + max(streak.a, cross_streak.a) * gain * (1.0 - base.a));
    return float4(output_rgb, output_alpha);
}
technique GlareComposite { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSGlare(v_in); } }
technique Draw { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSGlare(v_in); } }
)FXMFX";
static constexpr const char *kEmbeddedHalationEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d blurredImage;
uniform float2 texelSize;
uniform float4 effectColor;
uniform float4 secondaryColor;
uniform float opacity;
uniform float amount;
uniform float intensity;
uniform float softness;

sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v_in) { VertDataOut o; o.pos=mul(float4(v_in.pos.xyz,1.0),ViewProj); o.uv=v_in.uv; return o; }
float4 PSHalation(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float2 shift = texelSize * (1.0 + saturate(softness) * 3.0);
    float4 broad = blurredImage.Sample(textureSampler, v_in.uv);
    float red = blurredImage.Sample(textureSampler, v_in.uv + float2(shift.x, 0.0)).r;
    float green = blurredImage.Sample(textureSampler, v_in.uv - float2(0.0, shift.y)).g;
    float3 halo = max(broad.rgb - base.rgb * 0.35, float3(0.0, 0.0, 0.0));
    halo.r = max(halo.r, red);
    halo.g = max(halo.g * 0.42, green * 0.20);
    halo.b *= 0.10;
    float3 tint = lerp(effectColor.rgb, secondaryColor.rgb, 0.18);
    float gain = max(intensity, 0.0) * max(amount, 0.0) * clamp(opacity, 0.0, 1.0) * effectColor.a;
    float3 output_rgb = base.rgb + halo * tint * gain;
    float output_alpha = saturate(base.a + broad.a * gain * 0.18 * (1.0 - base.a));
    return float4(output_rgb, output_alpha);
}
technique HalationComposite { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSHalation(v_in); } }
technique Draw { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSHalation(v_in); } }
)FXMFX";
static constexpr const char *kEmbeddedFinishingEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 texelSize;
uniform float opacity;
uniform float amount;
uniform float radius;
uniform float scale;
uniform float softness;
uniform float threshold;
uniform float angle;
uniform float evolution;
uniform float complexity;
uniform float2 center;
uniform float2 direction;
uniform float distortion;
uniform float4 effectColor;

sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v_in) { VertDataOut o; o.pos=mul(float4(v_in.pos.xyz,1.0),ViewProj); o.uv=v_in.uv; return o; }
float4 sample_image(float2 uv) { return image.Sample(textureSampler, uv); }
float2 rotate2(float2 p, float a) { float c=cos(a), s=sin(a); return float2(c*p.x-s*p.y, s*p.x+c*p.y); }
float4 mix_base(float4 base, float4 effected) { return lerp(base, effected, saturate(opacity)); }
float4 PSLensDistortion(VertDataOut v_in) : TARGET
{
    float2 p = v_in.uv - center;
    float aspect = texelSize.y / max(texelSize.x, 0.000001);
    p.x *= aspect;
    float r2 = dot(p,p);
    float factor = 1.0 + distortion * r2 + distortion * 0.35 * r2 * r2;
    p *= factor;
    p.x /= aspect;
    float4 base = sample_image(v_in.uv);
    return mix_base(base, sample_image(center + p));
}
float4 PSChromaticAberration(VertDataOut v_in) : TARGET
{
    float2 radial = v_in.uv - center;
    float len = length(radial);
    float2 axis = len > 0.00001 ? radial / len : direction;
    float2 shift = axis * texelSize * max(amount, 0.0) * (0.5 + len * 1.5);
    float4 base = sample_image(v_in.uv);
    float4 r = sample_image(v_in.uv + shift);
    float4 b = sample_image(v_in.uv - shift);
    float4 outv = float4(r.r, base.g, b.b, base.a);
    return mix_base(base, outv);
}
float4 PSDirectionalBlur(VertDataOut v_in) : TARGET
{
    float2 d = direction * texelSize * max(radius, 0.0) * 0.25;
    float4 sum = sample_image(v_in.uv) * 0.20;
    sum += (sample_image(v_in.uv+d)+sample_image(v_in.uv-d))*0.16;
    sum += (sample_image(v_in.uv+d*2.0)+sample_image(v_in.uv-d*2.0))*0.12;
    sum += (sample_image(v_in.uv+d*3.0)+sample_image(v_in.uv-d*3.0))*0.08;
    sum += (sample_image(v_in.uv+d*4.0)+sample_image(v_in.uv-d*4.0))*0.04;
    return mix_base(sample_image(v_in.uv), sum);
}
float4 PSZoomBlur(VertDataOut v_in) : TARGET
{
    float2 v = (center - v_in.uv) * max(radius,0.0) * 0.0025;
    float4 sum = sample_image(v_in.uv) * 0.20;
    sum += sample_image(v_in.uv+v)*0.16;
    sum += sample_image(v_in.uv+v*2.0)*0.14;
    sum += sample_image(v_in.uv+v*3.0)*0.12;
    sum += sample_image(v_in.uv+v*4.0)*0.10;
    sum += sample_image(v_in.uv+v*5.0)*0.08;
    sum += sample_image(v_in.uv+v*6.0)*0.06;
    sum += sample_image(v_in.uv+v*7.0)*0.04;
    sum += sample_image(v_in.uv+v*8.0)*0.02;
    return mix_base(sample_image(v_in.uv), sum);
}
float4 PSRadialBlur(VertDataOut v_in) : TARGET
{
    float2 p=v_in.uv-center;
    float a=max(radius,0.0)*0.00035;
    float4 sum=sample_image(v_in.uv)*0.20;
    sum+=(sample_image(center+rotate2(p,a))+sample_image(center+rotate2(p,-a)))*0.16;
    sum+=(sample_image(center+rotate2(p,a*2.0))+sample_image(center+rotate2(p,-a*2.0)))*0.12;
    sum+=(sample_image(center+rotate2(p,a*3.0))+sample_image(center+rotate2(p,-a*3.0)))*0.08;
    sum+=(sample_image(center+rotate2(p,a*4.0))+sample_image(center+rotate2(p,-a*4.0)))*0.04;
    return mix_base(sample_image(v_in.uv),sum);
}
float4 PSRipple(VertDataOut v_in) : TARGET
{
    float2 p=v_in.uv-center;
    float d=length(p);
    float2 n=d>0.00001?p/d:float2(0.0,0.0);
    float wave=sin(d*max(scale,0.001)*6.2831853+evolution)*amount;
    float2 uv=v_in.uv+n*wave*texelSize*8.0;
    return mix_base(sample_image(v_in.uv),sample_image(uv));
}
float4 PSWaveWarp(VertDataOut v_in) : TARGET
{
    float radians=angle*0.01745329252;
    float2 axis=float2(cos(radians),sin(radians));
    float2 perp=float2(-axis.y,axis.x);
    float phase=dot(v_in.uv,axis)*max(scale,0.001)*6.2831853+evolution;
    float2 uv=v_in.uv+perp*sin(phase)*amount*texelSize*8.0;
    return mix_base(sample_image(v_in.uv),sample_image(uv));
}
float4 PSPixelate(VertDataOut v_in) : TARGET
{
    float2 cells=max(float2(radius,radius),float2(1.0,1.0));
    float2 pixel=texelSize*cells;
    float2 uv=(floor(v_in.uv/pixel)+0.5)*pixel;
    return mix_base(sample_image(v_in.uv),image.Sample(pointSampler,uv));
}
float edge_luma(float3 c) { return dot(c,float3(0.2126,0.7152,0.0722)); }
float4 PSEdgeDetect(VertDataOut v_in) : TARGET
{
    float2 d=texelSize*max(radius,1.0);
    float gx=edge_luma(sample_image(v_in.uv+float2(d.x,0)).rgb)-edge_luma(sample_image(v_in.uv-float2(d.x,0)).rgb);
    float gy=edge_luma(sample_image(v_in.uv+float2(0,d.y)).rgb)-edge_luma(sample_image(v_in.uv-float2(0,d.y)).rgb);
    float edge=smoothstep(threshold,threshold+0.08,sqrt(gx*gx+gy*gy)*max(amount,0.0));
    float4 base=sample_image(v_in.uv);
    float4 outv=float4(effectColor.rgb*edge,base.a);
    return mix_base(base,outv);
}
float4 PSPosterize(VertDataOut v_in) : TARGET
{
    float4 base=sample_image(v_in.uv);
    float levels=max(floor(complexity+0.5),2.0);
    float3 straight=base.a>0.00001?base.rgb/base.a:float3(0.0,0.0,0.0);
    straight=floor(straight*(levels-1.0)+0.5)/(levels-1.0);
    return mix_base(base,float4(straight*base.a,base.a));
}
float4 PSThreshold(VertDataOut v_in) : TARGET
{
    float4 base=sample_image(v_in.uv);
    float3 straight=base.a>0.00001?base.rgb/base.a:float3(0.0,0.0,0.0);
    float y=edge_luma(straight);
    float v=smoothstep(threshold-softness*0.25,threshold+softness*0.25,y);
    float4 outv=float4(effectColor.rgb*v*base.a,base.a);
    return mix_base(base,outv);
}
float4 PSScanlines(VertDataOut v_in) : TARGET
{
    float4 base=sample_image(v_in.uv);
    float radians=angle*0.01745329252;
    float2 axis=float2(cos(radians),sin(radians));
    float coordinate=dot(v_in.uv/texelSize,axis);
    float stripe=0.5+0.5*cos(coordinate*6.2831853/max(scale,1.0)+evolution);
    stripe=smoothstep(softness,1.0,stripe);
    float darken=1.0-saturate(amount)*(1.0-stripe);
    return mix_base(base,float4(base.rgb*darken,base.a));
}
technique LensDistortion { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSLensDistortion(v_in); } }
technique ChromaticAberration { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSChromaticAberration(v_in); } }
technique DirectionalBlur { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSDirectionalBlur(v_in); } }
technique ZoomBlur { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSZoomBlur(v_in); } }
technique RadialBlur { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSRadialBlur(v_in); } }
technique Ripple { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSRipple(v_in); } }
technique WaveWarp { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSWaveWarp(v_in); } }
technique Pixelate { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSPixelate(v_in); } }
technique EdgeDetect { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSEdgeDetect(v_in); } }
technique Posterize { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSPosterize(v_in); } }
technique Threshold { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSThreshold(v_in); } }
technique Scanlines { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSScanlines(v_in); } }
technique Draw { pass { vertex_shader=VSDefault(v_in); pixel_shader=PSLensDistortion(v_in); } }
)FXMFX";
static constexpr const char *kEmbeddedVignetteEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 texelSize;
uniform float4 effectColor;
uniform float opacity;
uniform float amount;
uniform float scale;
uniform float softness;
uniform float roundness;
uniform float2 center;
uniform int invert;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertDataOut {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv = v_in.uv;
    return vert_out;
}

float4 PSVignette(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(textureSampler, v_in.uv);
    float2 p = (v_in.uv - center) * 2.0;
    float aspect = max(texelSize.y / max(texelSize.x, 0.000001), 0.0001);
    float shape_mix = saturate(roundness * 0.5 + 0.5);
    p.x *= lerp(aspect, 1.0, shape_mix);

    float d = length(p) / max(scale, 0.001);
    float feather = max(softness, 0.0001);
    float mask = smoothstep(1.0 - feather, 1.0 + feather, d);
    if (invert != 0)
        mask = 1.0 - mask;

    float mix_amount = saturate(mask * max(amount, 0.0) *
                                clamp(opacity, 0.0, 1.0) * effectColor.a);
    float3 straight = base.a > 0.0001 ? base.rgb / base.a : float3(0.0, 0.0, 0.0);
    straight = lerp(straight, effectColor.rgb, mix_amount);
    return float4(clamp(straight, 0.0, 1.0) * base.a, base.a);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSVignette(v_in);
    }
}
)FXMFX";
static constexpr const char *kEmbeddedNoiseEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float4 layerUvRect;
uniform float2 layerUvOrigin;
uniform float2 layerUvAxisX;
uniform float2 layerUvAxisY;
uniform float2 layerPixelSize;
uniform float4 effectColor;
uniform float opacity;
uniform float amount;
uniform float scale;
uniform float softness;
uniform float complexity;
uniform float spread;
uniform float falloff;
uniform float brightness;
uniform float contrast;
uniform float speed;
uniform float time;
uniform float evolution;
uniform float seed;
uniform float2 noiseOffset;
uniform float aspect;
uniform float lacunarity;
uniform float gain;
uniform int profile;
uniform int animatedNoise;
uniform int monochrome;
uniform int invert;
uniform int affectAlpha;
uniform int clampOutput;
uniform int temporalStability;

sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut o;
    o.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    o.uv = v_in.uv;
    return o;
}
float2 layer_space_uv(float2 uv) {
    float2 d=uv-layerUvOrigin; float det=layerUvAxisX.x*layerUvAxisY.y-layerUvAxisX.y*layerUvAxisY.x;
    if(abs(det)<=0.0000001){float2 e=max(abs(layerUvRect.zw),float2(0.000001,0.000001));return (uv-layerUvRect.xy)/e;}
    return float2((d.x*layerUvAxisY.y-d.y*layerUvAxisY.x)/det,(layerUvAxisX.x*d.y-layerUvAxisX.y*d.x)/det);
}
float hash1(float2 p){return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453);}
float2 grad2(float2 p){float2 g=float2(hash1(p+float2(17.17,3.11)),hash1(p+float2(43.73,29.41)))*2.0-1.0;return g/max(length(g),0.0001);}
float value_noise(float2 p){float2 c=floor(p),f=frac(p);f=f*f*(3.0-2.0*f);return lerp(lerp(hash1(c),hash1(c+float2(1,0)),f.x),lerp(hash1(c+float2(0,1)),hash1(c+1.0),f.x),f.y);}
float gradient_noise(float2 p){float2 c=floor(p),f=frac(p),u=f*f*f*(f*(f*6.0-15.0)+10.0);float a=dot(grad2(c),f),b=dot(grad2(c+float2(1,0)),f-float2(1,0)),cc=dot(grad2(c+float2(0,1)),f-float2(0,1)),d=dot(grad2(c+1.0),f-1.0);return saturate(lerp(lerp(a,b,u.x),lerp(cc,d,u.x),u.y)*0.72+0.5);}
float gaussian(float2 p){float s=hash1(p+1.17)+hash1(p+7.31)+hash1(p+13.73)+hash1(p+29.41)+hash1(p+47.83)+hash1(p+71.19);return saturate((s/6.0-0.5)*1.9+0.5);}
float cell_d(float2 c,float2 f,float2 n){float2 q=n+float2(hash1(c+n),hash1(c+n+47.17));return length(q-f);}
float cellular(float2 p){float2 c=floor(p),f=frac(p);float n=cell_d(c,f,float2(-1,-1));n=min(n,cell_d(c,f,float2(0,-1)));n=min(n,cell_d(c,f,float2(1,-1)));n=min(n,cell_d(c,f,float2(-1,0)));n=min(n,cell_d(c,f,float2(0,0)));n=min(n,cell_d(c,f,float2(1,0)));n=min(n,cell_d(c,f,float2(-1,1)));n=min(n,cell_d(c,f,float2(0,1)));n=min(n,cell_d(c,f,float2(1,1)));return saturate(n);}
float shape(float v,int m){if(m==1)return abs(v*2.0-1.0);if(m==2){float r=1.0-abs(v*2.0-1.0);return r*r;}return v;}
float fractal(float2 p,float oct,float lac,float g,int m){
    lac=max(lac,1.01);g=saturate(g);float2 p0=p,p1=p0*lac+13.1,p2=p1*lac+7.7,p3=p2*lac+19.3,p4=p3*lac+3.9,p5=p4*lac+29.1,p6=p5*lac+11.7,p7=p6*lac+41.3;
    float a0=1.0,a1=g,a2=a1*g,a3=a2*g,a4=a3*g,a5=a4*g,a6=a5*g,a7=a6*g;
    float e0=1.0,e1=step(1.5,oct),e2=step(2.5,oct),e3=step(3.5,oct),e4=step(4.5,oct),e5=step(5.5,oct),e6=step(6.5,oct),e7=step(7.5,oct);
    float v=shape(gradient_noise(p0),m)*a0*e0+shape(gradient_noise(p1),m)*a1*e1+shape(gradient_noise(p2),m)*a2*e2+shape(gradient_noise(p3),m)*a3*e3+shape(gradient_noise(p4),m)*a4*e4+shape(gradient_noise(p5),m)*a5*e5+shape(gradient_noise(p6),m)*a6*e6+shape(gradient_noise(p7),m)*a7*e7;
    return v/max(a0*e0+a1*e1+a2*e2+a3*e3+a4*e4+a5*e5+a6*e6+a7*e7,0.0001);
}
float blue(float2 px,float phase){float2 p=px+float2(phase*17.0,phase*31.0);return frac(52.9829189*frac(dot(p,float2(0.06711056,0.00583715))));}
float profile_noise(float2 p,float2 px,float phase,int mode){
    if(mode==0) return gaussian(floor(p*1.8));
    if(mode==1){float grain=gaussian(p*1.35);float clump=fractal(p*0.16,max(complexity,4.0),lacunarity,gain,0);return saturate(grain*0.72+clump*0.28);}
    if(mode==2){float sensor=gaussian(floor(p*1.15));float row=hash1(float2(floor(px.y/2.0),seed+phase))-0.5;float col=hash1(float2(floor(px.x/3.0),seed*2.0+phase))-0.5;return saturate(sensor+row*0.16+col*0.06);}
    if(mode==3) return fractal(p*0.13,complexity,lacunarity,gain,0);
    if(mode==4) return fractal(p*0.2,complexity,lacunarity,gain,1);
    if(mode==5) return fractal(p*0.18,complexity,lacunarity,gain,2);
    if(mode==6){float c=cellular(p*0.24);return saturate(1.0-c*0.85);}
    if(mode==7) return blue(floor(px),phase);
    return gaussian(floor(p));
}
float4 PSNoise(VertDataOut v_in):TARGET{
    float4 base=image.Sample(textureSampler,v_in.uv);float2 uv=layer_space_uv(v_in.uv);float phaseAnim=0.0;
    if(animatedNoise!=0){phaseAnim=time*speed;if(temporalStability!=0)phaseAnim=floor(phaseAnim*60.0+0.5)/60.0;}
    float phase=evolution+seed*17.13+phaseAnim;float2 px=uv*max(layerPixelSize,float2(1,1));float ar=pow(2.0,clamp(aspect,-3.0,3.0));float2 p=(px+noiseOffset)/max(scale,0.001);p=float2(p.x*ar,p.y/ar)+float2(phase,phase*0.731);
    float strength=max(amount,0.0)*clamp(opacity,0.0,1.0);float3 straight=base.a>0.000001?base.rgb/base.a:float3(0,0,0);
    float v=profile_noise(p,px,phaseAnim,profile);float3 n=float3(v,v,v);if(monochrome==0){n.g=profile_noise(p+float2(37,17),px+37,phaseAnim+11,profile);n.b=profile_noise(p+float2(91,53),px+91,phaseAnim+23,profile);}
    n=lerp(n,float3(0.5,0.5,0.5),saturate(softness));n=(n-0.5)*max(contrast,0.0)+0.5+brightness*0.5;if(invert!=0)n=1.0-n;
    float3 weight=max(effectColor.rgb,float3(0,0,0));float a=base.a;if(affectAlpha!=0)a=base.a+(n.r-0.5)*strength*effectColor.a;if(clampOutput!=0)a=saturate(a);
    float3 result=(straight+(n-0.5)*strength*weight)*a;if(clampOutput!=0)result=clamp(result,0.0,a);return float4(result,a);
}
technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSNoise(v_in);
    }
}
)FXMFX";

static constexpr const char *kEmbeddedDamageDistortionEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float4 layerUvRect;
uniform float2 layerUvOrigin;
uniform float2 layerUvAxisX;
uniform float2 layerUvAxisY;
uniform float2 layerPixelSize;
uniform float4 effectColor;
uniform float4 secondaryColor;
uniform float opacity;
uniform float amount;
uniform float scale;
uniform float softness;
uniform float complexity;
uniform float spread;
uniform float falloff;
uniform float brightness;
uniform float contrast;
uniform float speed;
uniform float time;
uniform float evolution;
uniform float seed;
uniform float2 noiseOffset;
uniform float aspect;
uniform int profile;
uniform int damageProfile;
uniform int animatedNoise;
uniform int clampOutput;
uniform int temporalStability;

sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };

struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut o;
    o.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    o.uv = v_in.uv;
    return o;
}

float2 layer_space_uv(float2 uv)
{
    float2 d = uv - layerUvOrigin;
    float det = layerUvAxisX.x * layerUvAxisY.y - layerUvAxisX.y * layerUvAxisY.x;
    if (abs(det) <= 0.0000001) {
        float2 e = max(abs(layerUvRect.zw), float2(0.000001, 0.000001));
        return (uv - layerUvRect.xy) / e;
    }
    return float2((d.x * layerUvAxisY.y - d.y * layerUvAxisY.x) / det,
                  (layerUvAxisX.x * d.y - layerUvAxisX.y * d.x) / det);
}

float2 texture_uv_from_layer_uv(float2 luv)
{
    return layerUvOrigin + layerUvAxisX * luv.x + layerUvAxisY * luv.y;
}

float hash1(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453123);
}

float signed_hash(float2 p)
{
    return hash1(p) * 2.0 - 1.0;
}

float gate(float value, float threshold, float soft)
{
    return smoothstep(threshold - soft, threshold + soft, value);
}

float line_mask(float x, float center, float width)
{
    return 1.0 - smoothstep(0.0, max(width, 0.00001), abs(x - center));
}

float phase_value()
{
    float p = evolution + seed * 13.37;
    if (animatedNoise != 0) {
        float t = time * speed;
        if (temporalStability != 0)
            t = floor(t * 60.0 + 0.5) / 60.0;
        p += t;
    }
    return p;
}

float4 sample_premul(float2 uv)
{
    return image.Sample(textureSampler, saturate(uv));
}

float3 straight_rgb(float4 c)
{
    return c.a > 0.000001 ? c.rgb / c.a : float3(0.0, 0.0, 0.0);
}

float4 premul_from_rgb(float3 rgb, float alpha)
{
    if (clampOutput != 0)
        rgb = saturate(rgb);
    return float4(rgb * alpha, alpha);
}

float grain_hash(float2 p)
{
    float n = hash1(p + 1.17) + hash1(p + 7.31) + hash1(p + 13.73) + hash1(p + 29.41);
    return saturate(n * 0.25);
}

float organic_noise(float2 p, float octaves)
{
    float amp = 0.55;
    float sum = 0.0;
    float norm = 0.0;
    float2 q = p;
    for (int i = 0; i < 5; ++i) {
        if ((float)i >= octaves)
            break;
        sum += grain_hash(q) * amp;
        norm += amp;
        q = q * 2.17 + float2(19.1, 7.3);
        amp *= 0.52;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

float3 color_correct(float3 rgb, float strength)
{
    rgb = (rgb - 0.5) * max(contrast, 0.0) + 0.5 + brightness * 0.35 * strength;
    return rgb;
}

/* legacy token: film_scratches; organic replacement: film_vertical_scratch */
float film_vertical_scratch(float2 luv, float2 px, float frame, float id, float strength)
{
    float base_col = hash1(float2(id, seed + floor(frame * 0.35)));
    float wander = sin(luv.y * (5.0 + id * 1.7) + frame * (0.11 + id * 0.03) + seed) * (0.002 + 0.012 * falloff);
    float center = base_col + wander + signed_hash(float2(id, frame)) * 0.012;
    float width = (0.0009 + 0.0065 * softness) * (1.0 + strength * (0.6 + id * 0.11));
    float broken = gate(hash1(float2(floor(px.y / max(12.0, scale * (3.0 + id))), floor(frame * 0.5) + id)),
                        0.42 + strength * 0.08, 0.16 + softness * 0.16);
    float active = gate(hash1(float2(id, floor(frame * 0.12) + seed)), 0.64 - strength * 0.28, 0.05 + softness * 0.08);
    return line_mask(luv.x, center, width) * broken * active;
}

float film_hair_fiber(float2 luv, float2 px, float frame, float strength)
{
    float y = frac(hash1(float2(floor(frame * 0.2), seed + 88.0)) + signed_hash(float2(seed, frame)) * 0.04);
    float wave = sin((luv.x * 15.0 + frame * 0.04 + seed) * (0.7 + falloff)) * 0.018 * strength;
    float fiber = line_mask(luv.y, y + wave, 0.0018 + 0.010 * softness);
    float segment = gate(hash1(float2(floor(luv.x * 18.0), floor(frame * 0.4) + seed)), 0.28, 0.12);
    float active = gate(hash1(float2(floor(frame * 0.08), seed + 99.0)), 0.78 - strength * 0.28, 0.05);
    return fiber * segment * active;
}

float film_dust_blob(float2 luv, float2 px, float frame, float strength)
{
    float cell_px = max(4.0, scale * 6.0);
    float2 cell = floor((px + noiseOffset) / cell_px);
    float2 local = frac((px + noiseOffset) / cell_px) - 0.5;
    float roundness = length(local * float2(1.0 + signed_hash(cell) * 0.45, 1.0));
    float blob = 1.0 - smoothstep(0.04, 0.22 + softness * 0.42, roundness);
    float persistent = gate(hash1(cell + float2(seed, floor(frame * 0.05))), 0.83 - strength * 0.32, 0.035 + softness * 0.05);
    float sparkle = gate(hash1(cell + float2(floor(frame * 0.9), seed)), 0.57, 0.18);
    return blob * persistent * sparkle;
}

float film_blotch(float2 luv, float frame, float strength)
{
    float2 stain_pos = float2(hash1(float2(floor(frame * 0.06), seed + 3.0)),
                              hash1(float2(seed + 4.0, floor(frame * 0.07))));
    float2 d = (luv - stain_pos) * float2(1.0, 1.35);
    float shape = 1.0 - smoothstep(0.035, 0.28 + falloff * 0.22, length(d));
    float edge = organic_noise(luv * (18.0 + complexity * 3.0) + frame * 0.05, 4.0);
    float active = gate(hash1(float2(floor(frame * 0.05), seed + 37.0)), 0.86 - strength * 0.20, 0.05);
    return shape * gate(edge, 0.38, 0.22) * active;
}

float4 composite_film(VertDataOut v_in, float2 luv, float2 px, float strength)
{
    float phase = phase_value();
    float frame = floor(phase * 18.0);
    float weave_x = signed_hash(float2(frame, seed)) * (0.0025 + 0.015 * falloff) * strength;
    float weave_y = signed_hash(float2(frame + 19.0, seed)) * (0.0015 + 0.006 * falloff) * strength;
    float flutter = sin((luv.y * (7.0 + complexity * 1.3) + phase * 6.28318)) * 0.0025 * strength;
    float4 src = sample_premul(v_in.uv + float2(weave_x + flutter, weave_y));
    float alpha = src.a;
    float3 rgb = straight_rgb(src);

    float scratches = 0.0;
    scratches += film_vertical_scratch(luv, px, frame, 2.0, strength);
    scratches += film_vertical_scratch(luv, px, frame, 7.0, strength) * 0.7;
    scratches += film_vertical_scratch(luv, px, frame, 13.0, strength) * 0.45;
    scratches = saturate(scratches);
    float fiber = film_hair_fiber(luv, px, frame, strength);
    float dust = film_dust_blob(luv, px, frame, strength);
    float blotch = film_blotch(luv, frame, strength);
    float emulsion_grain = (organic_noise(px / max(1.0, scale * 1.25) + frame * 0.17, 4.0) - 0.5) * 0.25 * strength;
    float flicker = signed_hash(float2(frame, seed + 19.0)) * 0.20 * strength +
                    sin(phase * 6.28318 * 0.71 + seed) * 0.045 * strength;
    float vignette = smoothstep(0.24, 0.92, distance(luv, float2(0.5, 0.5)));

    rgb *= 1.0 + flicker;
    rgb += emulsion_grain;
    rgb = lerp(rgb, rgb * (0.78 - vignette * 0.18), strength * vignette * 0.45);
    rgb = lerp(rgb, effectColor.rgb, scratches * strength * (0.55 + softness * 0.35));
    rgb = lerp(rgb, float3(0.02, 0.015, 0.01), dust * strength * (0.55 + falloff * 0.25));
    rgb = lerp(rgb, effectColor.rgb * 0.86, fiber * strength * 0.35);
    rgb = lerp(rgb, secondaryColor.rgb, blotch * strength * 0.38);
    rgb = color_correct(rgb, strength);
    return premul_from_rgb(rgb, alpha);
}

float2 analog_yiq(float3 rgb)
{
    return float2(dot(rgb, float3(0.596, -0.274, -0.322)),
                  dot(rgb, float3(0.211, -0.523, 0.312)));
}

float3 analog_from_yiq(float y, float2 iq)
{
    return float3(y + 0.956 * iq.x + 0.621 * iq.y,
                  y - 0.272 * iq.x - 0.647 * iq.y,
                  y - 1.106 * iq.x + 1.703 * iq.y);
}

float analog_dropout(float2 px, float frame, float strength)
{
    float row_h = max(2.0, scale * (0.42 + softness));
    float row = floor(px.y / row_h);
    float bands = gate(hash1(float2(row, floor(frame * 0.72) + seed)), 0.74 - strength * 0.34, 0.02 + softness * 0.08);
    float breakup = gate(hash1(float2(floor(px.x / max(16.0, scale * 8.0)), row + frame)), 0.46, 0.22);
    return bands * breakup;
}

float tracking_band(float2 luv, float2 px, float frame, float strength)
{
    float bottom = smoothstep(0.76, 1.0, luv.y);
    float head = sin((luv.y * 80.0 + frame * 0.8 + seed) * 0.42) * 0.5 + 0.5;
    float random_bands = gate(hash1(float2(floor(px.y / max(3.0, scale * 0.7)), frame + seed)), 0.80 - strength * 0.30, 0.035 + softness * 0.08);
    return saturate(bottom * head * strength + random_bands * strength * 0.65);
}

/* legacy token: scan; analog path still renders scanlines/interlacing/chroma */
float4 composite_analog(VertDataOut v_in, float2 luv, float2 px, float strength)
{
    float phase = phase_value();
    float frame = floor(phase * 29.97);
    float row = floor(px.y / max(2.0, scale * 0.45));
    float sync = gate(hash1(float2(row, floor(frame * 0.7) + seed)), 0.78 - strength * 0.36, 0.025 + softness * 0.08);
    float fine_wobble = sin(luv.y * (18.0 + complexity * 3.0) + phase * 6.28318) * (0.0015 + 0.006 * strength);
    float slow_wobble = sin(luv.y * (3.2 + falloff * 6.0) + phase * 2.2 + seed) * (0.002 + 0.012 * strength);
    float head_switch = tracking_band(luv, px, frame, strength) * (0.012 + 0.065 * strength);
    float tear = signed_hash(float2(row, frame + seed * 3.0)) * sync * (0.004 + 0.060 * strength);
    float2 warped = v_in.uv + float2(fine_wobble + slow_wobble + tear + head_switch, 0.0);

    float chroma = (0.0015 + 0.010 * falloff + 0.002 * softness) * strength;
    float4 y_src = sample_premul(warped);
    float3 y_rgb = straight_rgb(y_src);
    float y = dot(y_rgb, float3(0.299, 0.587, 0.114));
    float3 c_left = straight_rgb(sample_premul(warped - float2(chroma * 1.8, 0.0)));
    float3 c_right = straight_rgb(sample_premul(warped + float2(chroma * 1.8, 0.0)));
    float2 iq = (analog_yiq(c_left) + analog_yiq(c_right)) * 0.5;
    float3 rgb = analog_from_yiq(y, iq);

    float scanline = 0.84 + 0.16 * sin(px.y * 3.14159265);
    float interlace = (frac((px.y + floor(frame)) * 0.5) < 0.5) ? 0.92 : 1.04;
    float dropout = analog_dropout(px, frame, strength);
    float static_fine = (grain_hash(px * float2(0.73, 0.91) + frame + seed) - 0.5) * 0.32 * strength;
    float chroma_crawl = sin(px.x * 0.19 + frame * 0.75 + seed) * 0.035 * strength;

    rgb *= lerp(1.0, scanline * interlace, saturate(strength * 0.95 + softness * 0.5));
    rgb.r += chroma_crawl;
    rgb.b -= chroma_crawl * 0.75;
    rgb += static_fine;
    rgb = lerp(rgb, secondaryColor.rgb, dropout * strength * 0.28);
    rgb -= dropout * strength * 0.28;
    rgb = color_correct(rgb, strength);
    return premul_from_rgb(rgb, y_src.a);
}

/* legacy token: macroblock; macroblock compression artifact selector */
float digital_block_mask(float2 block, float frame, float strength)
{
    float a = gate(hash1(block + float2(frame, seed)), 0.68 - strength * 0.32, 0.025 + softness * 0.08);
    float b = gate(hash1(float2(block.y, floor(frame * 0.37) + seed)), 0.80 - strength * 0.22, 0.04);
    return saturate(max(a, b * 0.75));
}

float3 digital_ringing(float2 uv, float2 luv, float2 block_count, float strength)
{
    float2 step_uv = abs(layerUvAxisX) / max(block_count.x, 1.0) + abs(layerUvAxisY) / max(block_count.y, 1.0);
    float3 c0 = straight_rgb(sample_premul(uv));
    float3 cx1 = straight_rgb(sample_premul(uv + float2(step_uv.x, 0.0)));
    float3 cx2 = straight_rgb(sample_premul(uv - float2(step_uv.x, 0.0)));
    float3 cy1 = straight_rgb(sample_premul(uv + float2(0.0, step_uv.y)));
    float3 cy2 = straight_rgb(sample_premul(uv - float2(0.0, step_uv.y)));
    float3 edge = c0 * 4.0 - cx1 - cx2 - cy1 - cy2;
    return edge * (0.10 + 0.35 * falloff) * strength;
}

float4 composite_digital(VertDataOut v_in, float2 luv, float2 px, float strength)
{
    float phase = phase_value();
    float frame = floor(phase * 18.0);
    float block_px = max(4.0, scale * (0.7 + softness * 1.8));
    float2 block_count = max(float2(4.0, 4.0), layerPixelSize / block_px);
    float2 block = floor(luv * block_count + noiseOffset * 0.001);
    float2 block_local = frac(luv * block_count + noiseOffset * 0.001);
    float glitch = digital_block_mask(block, frame, strength);
    float row = floor(px.y / max(4.0, block_px * 0.75));
    float row_glitch = gate(hash1(float2(row, floor(frame * 0.63) + seed * 5.0)), 0.78 - strength * 0.26, 0.025 + softness * 0.06);
    float2 packet_jump = float2(signed_hash(block + float2(seed, frame)) * 0.040,
                                signed_hash(block + float2(frame, seed)) * 0.015) * strength * max(glitch, row_glitch);
    packet_jump.x += signed_hash(float2(row, frame + seed)) * row_glitch * 0.055 * strength;

    float4 src = sample_premul(v_in.uv + packet_jump);
    float alpha = src.a;
    float3 rgb = straight_rgb(src);
    float2 center_luv = (block + 0.5) / block_count;
    float2 block_uv = texture_uv_from_layer_uv(center_luv);
    float3 block_rgb = straight_rgb(sample_premul(block_uv + packet_jump * 0.35));
    float block_mix = max(glitch, row_glitch) * saturate(0.32 + strength * 0.55);
    rgb = lerp(rgb, block_rgb, block_mix);

    float quant_levels = max(3.0, lerp(96.0, 5.0, saturate(strength * (0.72 + falloff * 0.75))));
    rgb = floor(rgb * quant_levels + 0.5) / quant_levels;
    float block_edge = max(smoothstep(0.0, 0.10, block_local.x) * (1.0 - smoothstep(0.88, 1.0, block_local.x)),
                           smoothstep(0.0, 0.10, block_local.y) * (1.0 - smoothstep(0.88, 1.0, block_local.y)));
    float edge_line = (1.0 - block_edge) * glitch * strength * 0.18;
    rgb += digital_ringing(v_in.uv + packet_jump, luv, block_count, strength) * max(glitch, row_glitch);
    rgb -= edge_line;

    float packet = gate(hash1(block + float2(seed * 2.0, frame * 7.0)), 0.86 - strength * 0.28, 0.018 + softness * 0.06);
    float sparkle = signed_hash(px * 0.37 + float2(frame, seed)) * packet * strength * 0.20;
    rgb += sparkle;
    rgb = lerp(rgb, effectColor.rgb, packet * strength * 0.28);
    rgb = lerp(rgb, secondaryColor.rgb, glitch * strength * 0.12);
    rgb = color_correct(rgb, strength);
    return premul_from_rgb(rgb, alpha);
}

float4 PSDamage(VertDataOut v_in) : TARGET
{
    float2 luv = layer_space_uv(v_in.uv);
    float2 px = luv * max(layerPixelSize, float2(1.0, 1.0));
    float strength = saturate(amount * opacity);
    int p = damageProfile;
    if (p < 0)
        p = profile >= 10 ? 2 : (profile >= 9 ? 1 : 0);
    if (p == 1)
        return composite_analog(v_in, luv, px, strength);
    if (p == 2)
        return composite_digital(v_in, luv, px, strength);
    return composite_film(v_in, luv, px, strength);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSDamage(v_in);
    }
}

)FXMFX";

static constexpr const char *kEmbeddedRoughenEdgesEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 texelSize;
uniform float opacity;
uniform float amount;
uniform float scale;
uniform float softness;
uniform float complexity;
uniform float evolution;
uniform float seed;
uniform int invert;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertDataOut {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv = v_in.uv;
    return vert_out;
}

float rough_hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float rough_value(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = rough_hash(cell);
    float b = rough_hash(cell + float2(1.0, 0.0));
    float c = rough_hash(cell + float2(0.0, 1.0));
    float d = rough_hash(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float rough_layers(float2 p, float layers)
{
    float value = rough_value(p) * 0.60;
    value += rough_value(p * 2.03 + 19.0) * 0.27 * step(1.5, layers);
    value += rough_value(p * 4.11 + 37.0) * 0.13 * step(2.5, layers);
    return value;
}

float4 PSRoughen(VertDataOut v_in) : TARGET
{
    float4 original = image.Sample(textureSampler, v_in.uv);
    float2 p = v_in.uv * max(scale, 0.001) +
               float2(evolution + seed * 3.1, evolution * 0.73 + seed * 7.7);
    float n1 = rough_layers(p, clamp(complexity, 1.0, 3.0)) - 0.5;
    float n2 = rough_layers(p + float2(41.7, 23.9), clamp(complexity, 1.0, 3.0)) - 0.5;
    if (invert != 0) {
        n1 = -n1;
        n2 = -n2;
    }

    float displacement = max(amount, 0.0) * 64.0;
    float2 offset = float2(n1, n2) * texelSize * displacement;
    float4 displaced = image.Sample(textureSampler, v_in.uv + offset);

    float a0 = original.a;
    float a1 = image.Sample(textureSampler, v_in.uv + float2(texelSize.x, 0.0)).a;
    float a2 = image.Sample(textureSampler, v_in.uv - float2(texelSize.x, 0.0)).a;
    float a3 = image.Sample(textureSampler, v_in.uv + float2(0.0, texelSize.y)).a;
    float a4 = image.Sample(textureSampler, v_in.uv - float2(0.0, texelSize.y)).a;
    float edge = max(max(abs(a0 - a1), abs(a0 - a2)), max(abs(a0 - a3), abs(a0 - a4)));
    float edge_mask = smoothstep(0.0, max(softness, 0.001), edge + abs(n1) * max(amount, 0.0));
    float keep = saturate(1.0 - edge_mask * max(amount, 0.0));
    float old_alpha = displaced.a;
    displaced.a *= keep;
    if (old_alpha > 0.0001)
        displaced.rgb *= displaced.a / old_alpha;
    return lerp(original, displaced, clamp(opacity, 0.0, 1.0));
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSRoughen(v_in);
    }
}
)FXMFX";

static constexpr const char *kEmbeddedSourceEffectsEffect = R"FXMFX(uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d sourceImage;
uniform float2 texelSize;
uniform float2 sourceTexelSize;
uniform float4 effectColor;
uniform float opacity;
uniform float radius;
uniform float amount;
uniform float distance;
uniform float spread;
uniform float falloff;
uniform float2 layerUvOrigin;
uniform float2 layerUvAxisX;
uniform float2 layerUvAxisY;
uniform int sourceEnabled;
uniform int sourceIsComposition;
uniform int xChannel;
uniform int yChannel;
uniform int wrapMode;
uniform int mappingSpace;
uniform int alphaAware;
uniform int inputIsComposition;

sampler_state linearSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VertDataOut VSDefault(VertDataIn v_in)
{
    VertDataOut o;
    o.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    o.uv = v_in.uv;
    return o;
}

float source_luma(float3 value)
{
    return dot(value, float3(0.2126, 0.7152, 0.0722));
}

float3 source_straight(float4 value)
{
    return value.a > 0.00001 ? value.rgb / value.a : float3(0.0, 0.0, 0.0);
}

float2 composition_uv(float2 local_uv)
{
    if (inputIsComposition != 0)
        return local_uv;
    return layerUvOrigin + layerUvAxisX * local_uv.x +
           layerUvAxisY * local_uv.y;
}

float2 source_space_uv(float2 input_uv)
{
    if (inputIsComposition == 0)
        return input_uv;
    float2 delta = input_uv - layerUvOrigin;
    float determinant = layerUvAxisX.x * layerUvAxisY.y -
                        layerUvAxisX.y * layerUvAxisY.x;
    if (abs(determinant) <= 0.0000001)
        return input_uv;
    return float2(
        (delta.x * layerUvAxisY.y - delta.y * layerUvAxisY.x) / determinant,
        (layerUvAxisX.x * delta.y - layerUvAxisX.y * delta.x) / determinant);
}

float source_channel(float4 value, int channel)
{
    float3 straight_value = source_straight(value);
    if (channel == 1) return straight_value.r;
    if (channel == 2) return straight_value.g;
    if (channel == 3) return straight_value.b;
    if (channel == 4) return value.a;
    return source_luma(straight_value);
}

float mirror_coordinate(float value)
{
    float tile = floor(value);
    float local_value = frac(value);
    float parity = step(1.0, frac(abs(tile) * 0.5) * 2.0);
    return lerp(local_value, 1.0 - local_value, parity);
}

float2 wrapped_uv(float2 uv)
{
    if (wrapMode == 1)
        return frac(uv);
    if (wrapMode == 2)
        return float2(mirror_coordinate(uv.x), mirror_coordinate(uv.y));
    return clamp(uv, float2(0.0, 0.0), float2(1.0, 1.0));
}

float uv_inside(float2 uv)
{
    return step(0.0, uv.x) * step(uv.x, 1.0) *
           step(0.0, uv.y) * step(uv.y, 1.0);
}

float4 PSDisplacementMap(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(linearSampler, v_in.uv);
    if (sourceEnabled == 0)
        return base;

    float2 source_uv = sourceIsComposition != 0
        ? composition_uv(v_in.uv) : source_space_uv(v_in.uv);
    float4 map_value = sourceImage.Sample(linearSampler, source_uv);
    float dx = source_channel(map_value, xChannel) - 0.5;
    float dy = source_channel(map_value, yChannel) - 0.5;
    float2 displaced_uv = v_in.uv + float2(
        dx * amount * texelSize.x,
        dy * distance * texelSize.y);
    float valid = wrapMode == 3 ? uv_inside(displaced_uv) : 1.0;
    float4 displaced = image.Sample(linearSampler, wrapped_uv(displaced_uv));
    displaced *= valid;
    return lerp(base, displaced, saturate(opacity));
}

float4 PSLightWrap(VertDataOut v_in) : TARGET
{
    float4 base = image.Sample(linearSampler, v_in.uv);
    if (sourceEnabled == 0 || base.a <= 0.00001)
        return base;

    float2 background_uv = sourceIsComposition != 0
        ? composition_uv(v_in.uv) : v_in.uv;
    float2 bg_step = sourceTexelSize * max(radius, 0.0);
    float4 background = sourceImage.Sample(linearSampler, background_uv);
    background += sourceImage.Sample(linearSampler, background_uv + float2(bg_step.x, 0.0));
    background += sourceImage.Sample(linearSampler, background_uv - float2(bg_step.x, 0.0));
    background += sourceImage.Sample(linearSampler, background_uv + float2(0.0, bg_step.y));
    background += sourceImage.Sample(linearSampler, background_uv - float2(0.0, bg_step.y));
    background += sourceImage.Sample(linearSampler, background_uv + bg_step);
    background += sourceImage.Sample(linearSampler, background_uv - bg_step);
    background += sourceImage.Sample(linearSampler, background_uv + float2(bg_step.x, -bg_step.y));
    background += sourceImage.Sample(linearSampler, background_uv + float2(-bg_step.x, bg_step.y));
    background /= 9.0;

    float2 edge_step = texelSize * max(spread, 1.0);
    float min_alpha = base.a;
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv + float2(edge_step.x, 0.0)).a);
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv - float2(edge_step.x, 0.0)).a);
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv + float2(0.0, edge_step.y)).a);
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv - float2(0.0, edge_step.y)).a);
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv + edge_step).a);
    min_alpha = min(min_alpha, image.Sample(linearSampler, v_in.uv - edge_step).a);

    float3 base_straight = source_straight(base);
    float luminance_edge = saturate(abs(source_luma(base_straight) -
        source_luma(source_straight(image.Sample(
            linearSampler, v_in.uv + float2(edge_step.x, 0.0))))));
    float alpha_edge = base.a * saturate(1.0 - min_alpha);
    float edge = alphaAware != 0 ? alpha_edge : max(alpha_edge, luminance_edge);
    float protection = saturate(1.0 - source_luma(base_straight) * saturate(falloff));
    float mix_amount = saturate(edge * max(amount, 0.0) *
                                saturate(opacity) * protection);

    float3 background_straight = source_straight(background);
    float3 tinted_spill = lerp(background_straight,
                               background_straight * effectColor.rgb,
                               saturate(effectColor.a));
    float3 result_straight = lerp(base_straight, tinted_spill, mix_amount);
    return float4(result_straight * base.a, base.a);
}

technique LightWrap
{
    pass { vertex_shader = VSDefault(v_in); pixel_shader = PSLightWrap(v_in); }
}

technique DisplacementMap
{
    pass { vertex_shader = VSDefault(v_in); pixel_shader = PSDisplacementMap(v_in); }
}

technique Draw
{
    pass { vertex_shader = VSDefault(v_in); pixel_shader = PSDisplacementMap(v_in); }
}
)FXMFX";

static const char *embedded_effect_source(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::LensFlare: return kEmbeddedLensFlareEffect;
    case LayerEffectType::Vignette: return kEmbeddedVignetteEffect;
    case LayerEffectType::Noise:
    case LayerEffectType::Grain:
        return kEmbeddedNoiseEffect;
    case LayerEffectType::FilmDistortion:
    case LayerEffectType::AnalogDistortion:
    case LayerEffectType::DigitalDistortion:
        /* Development Version 260: damage effects depend on packaged artifact
         * textures, so compile the installed shader asset rather than the
         * embedded procedural fallback. */
        return nullptr;
    case LayerEffectType::RoughenEdges: return kEmbeddedRoughenEdgesEffect;
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen: return kEmbeddedDetailEffect;
    case LayerEffectType::Glare: return kEmbeddedGlareEffect;
    case LayerEffectType::Halation: return kEmbeddedHalationEffect;
    case LayerEffectType::LightWrap:
    case LayerEffectType::DisplacementMap: return kEmbeddedSourceEffectsEffect;
    case LayerEffectType::LensDistortion:
    case LayerEffectType::ChromaticAberration:
    case LayerEffectType::DirectionalBlur:
    case LayerEffectType::ZoomBlur:
    case LayerEffectType::RadialBlur:
    case LayerEffectType::Ripple:
    case LayerEffectType::WaveWarp:
    case LayerEffectType::Pixelate:
    case LayerEffectType::EdgeDetect:
    case LayerEffectType::Posterize:
    case LayerEffectType::Threshold:
    case LayerEffectType::Scanlines: return kEmbeddedFinishingEffect;
    default: return nullptr;
    }
}

static const char *embedded_effect_name(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::LensFlare: return "embedded-fxm-lens-flare.effect";
    case LayerEffectType::Vignette: return "embedded-fxm-vignette.effect";
    case LayerEffectType::Noise:
    case LayerEffectType::Grain:
        return "embedded-fxm-noise.effect";
    case LayerEffectType::FilmDistortion:
    case LayerEffectType::AnalogDistortion:
    case LayerEffectType::DigitalDistortion:
        return "embedded-fxm-damage-distortion.effect";
    case LayerEffectType::RoughenEdges: return "embedded-fxm-roughen-edges.effect";
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen: return "embedded-fxm-detail.effect";
    case LayerEffectType::Glare: return "embedded-fxm-glare.effect";
    case LayerEffectType::Halation: return "embedded-fxm-halation.effect";
    case LayerEffectType::LightWrap:
    case LayerEffectType::DisplacementMap: return "embedded-fxm-source-effects.effect";
    case LayerEffectType::LensDistortion:
    case LayerEffectType::ChromaticAberration:
    case LayerEffectType::DirectionalBlur:
    case LayerEffectType::ZoomBlur:
    case LayerEffectType::RadialBlur:
    case LayerEffectType::Ripple:
    case LayerEffectType::WaveWarp:
    case LayerEffectType::Pixelate:
    case LayerEffectType::EdgeDetect:
    case LayerEffectType::Posterize:
    case LayerEffectType::Threshold:
    case LayerEffectType::Scanlines: return "embedded-fxm-finishing.effect";
    default: return "embedded-fxm-effect.effect";
    }
}

} // namespace

TitleEffectRegistry::~TitleEffectRegistry()
{
    reset();
}

void TitleEffectRegistry::reset()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (gs_effect_t *&effect : builtins_) {
        if (effect)
            gs_effect_destroy(effect);
        effect = nullptr;
    }
    for (auto &entry : extensions_) {
        if (entry.second)
            gs_effect_destroy(entry.second);
    }
    extensions_.clear();
}


gs_effect_t *TitleEffectRegistry::find(LayerEffectType type) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t index = static_cast<std::size_t>(type);
    if (index >= builtins_.size())
        return nullptr;
    return builtins_[index];
}

gs_effect_t *TitleEffectRegistry::find(const std::string &stable_id) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto existing = extensions_.find(stable_id);
    return existing == extensions_.end() ? nullptr : existing->second;
}

gs_effect_t *TitleEffectRegistry::compile(LayerEffectType type)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    last_error_ = nullptr;
    const TitleEffectDefinition *def = definition(type);
    if (!def) {
        last_error_ = "Unknown effect type.";
        return nullptr;
    }

    const std::size_t index = static_cast<std::size_t>(type);
    if (index >= builtins_.size()) {
        last_error_ = "Effect type is outside the built-in registry.";
        return nullptr;
    }
    if (builtins_[index]) {
        fxm::perf::add(fxm::perf::Counter::EffectShaderCacheHits);
        return builtins_[index];
    }
    if (def->backend == EffectExecutionBackend::Cpu ||
        !def->relative_path || !*def->relative_path) {
        last_error_ = "CPU/geometry effects do not compile a GPU shader.";
        return nullptr;
    }
    fxm::perf::add(fxm::perf::Counter::EffectShaderCacheMisses);

    /* Shader compilation is registry-owned and happens only on the first use.
     * Render passes receive an already compiled handle and never compile or
     * resolve files in the frame hot path. */
    if (const char *embedded = embedded_effect_source(type)) {
        char *errors = nullptr;
        gs_effect_t *effect = gs_effect_create(
            embedded, embedded_effect_name(type), &errors);
        if (effect) {
            if (errors)
                bfree(errors);
            FXM_LOG_INFO("Effects", QStringLiteral("Compiled embedded procedural effect %1")
                                        .arg(QString::fromUtf8(def->stable_id)));
            builtins_[index] = effect;
            return effect;
        }
        FXM_LOG_WARNING("Effects", QStringLiteral("Embedded procedural effect %1 failed to compile: %2; trying installed asset")
                                       .arg(QString::fromUtf8(def->stable_id),
                                            QString::fromUtf8(errors ? errors : "unknown shader error")));
        blog(LOG_WARNING,
             "[Flux Motion] Embedded effect '%s' failed to compile: %s; trying installed asset",
             def->stable_id, errors ? errors : "unknown shader error");
        if (errors)
            bfree(errors);
    }

    const std::string resolved_path =
        fxm::resolve_asset_path(def->relative_path);
    if (resolved_path.empty()) {
        FXM_LOG_WARNING("Effects", QStringLiteral("Effect asset path could not be resolved for %1 (%2)")
                                       .arg(QString::fromUtf8(def->stable_id),
                                            QString::fromUtf8(def->relative_path)));
        last_error_ = "Effect asset path could not be resolved.";
        return nullptr;
    }

    char *errors = nullptr;
    gs_effect_t *effect = gs_effect_create_from_file(
        resolved_path.c_str(), &errors);
    if (!effect) {
        FXM_LOG_WARNING("Effects", QStringLiteral("Failed to compile effect %1 from %2: %3")
                                       .arg(QString::fromUtf8(def->stable_id),
                                             QString::fromStdString(resolved_path),
                                            QString::fromUtf8(errors ? errors : "unknown shader error")));
        last_error_ = "Effect shader could not be compiled.";
        if (errors)
            bfree(errors);
        return nullptr;
    }

    if (errors)
        bfree(errors);
    FXM_LOG_DEBUG("Effects", QStringLiteral("Compiled effect %1 from %2")
                                 .arg(QString::fromUtf8(def->stable_id),
                                      QString::fromStdString(resolved_path)));
    builtins_[index] = effect;
    return effect;
}

gs_effect_t *TitleEffectRegistry::compile(const std::string &stable_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (stable_id.empty()) {
        last_error_ = "Empty effect extension id.";
        return nullptr;
    }
    LayerEffectType built_in_type{};
    if (FxmEffectExtensionCatalog::builtInTypeForId(QString::fromStdString(stable_id), &built_in_type))
        return compile(built_in_type);
    const auto existing = extensions_.find(stable_id);
    if (existing != extensions_.end()) {
        fxm::perf::add(fxm::perf::Counter::EffectShaderCacheHits);
        return existing->second;
    }
    fxm::perf::add(fxm::perf::Counter::EffectShaderCacheMisses);

    auto &catalog = FxmEffectExtensionCatalog::instance();
    if (catalog.effects().empty())
        catalog.reload();
    const auto *definition = catalog.find(QString::fromStdString(stable_id));
    if (definition && definition->builtIn)
        return compile(definition->builtInType);
    if (!definition) {
        last_error_ = "Effect extension is not installed.";
        return nullptr;
    }
    const QByteArray path = definition->shaderPath.toUtf8();
    char *errors = nullptr;
    gs_effect_t *effect = gs_effect_create_from_file(path.constData(), &errors);
    if (!effect) {
        FXM_LOG_WARNING("Extensions", QStringLiteral("Failed to compile extension effect %1: %2")
                                       .arg(definition->id, QString::fromUtf8(errors ? errors : "unknown shader error")));
        if (errors) bfree(errors);
        last_error_ = "Extension shader could not be compiled.";
        return nullptr;
    }
    if (errors) bfree(errors);
    extensions_.emplace(stable_id, effect);
    FXM_LOG_INFO("Extensions", QStringLiteral("Loaded effect extension %1 from %2")
                                 .arg(definition->id, definition->shaderPath));
    return effect;
}

const std::vector<TitleEffectDefinition> &TitleEffectRegistry::definitions()
{
    return builtin_effect_descriptors();
}

const TitleEffectDefinition *TitleEffectRegistry::definition(LayerEffectType type)
{
    return effect_descriptor(type);
}
