#include "title-gpu-text-renderer.h"
#include "title-gpu-text-sdf.h"
#include "title-text-layout-qt-font-registry.h"

#include <QByteArray>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QRectF>
#include <QTransform>

#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fxm::gpu_text {
namespace {

static inline gs_eparam_t *gpu_text_effect_param(gs_effect_t *effect,
                                                  const char *name) noexcept
{
    return effect && name ? gs_effect_get_param_by_name(effect, name) : nullptr;
}

/* OBS maps GS_DYNAMIC textures with discard semantics on supported backends,
 * so preserving an atlas requires rewriting the complete mapped page. Use
 * smaller pages while retaining the previous 32 MiB worst-case R8 capacity:
 * a newly typed glyph now uploads at most 1 MiB instead of 4 MiB. */
constexpr int kAtlasSize = 1024;
constexpr int kAtlasMaxPages = 32;
constexpr int kSdfSpread = 32;
constexpr int kAtlasGap = 1;
constexpr uint32_t kGradientLutWidth = 512;
constexpr float kPi = 3.14159265358979323846f;

static constexpr const char *kGpuTextEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d glyphAtlas;
uniform int coverageMode;
uniform int drawPart;
uniform float sdfSpread;
uniform float2 atlasTexelSize;
uniform float2 materialOrigin;
uniform float2 materialSize;
uniform texture2d fillGradientLut;
uniform texture2d strokeGradientLut;

uniform int fillType;
uniform int fillGradientType;
uniform int fillGradientSpread;
uniform float4 fillColor;
uniform float4 fillStartColor;
uniform float4 fillEndColor;
uniform float fillStartPos;
uniform float fillEndPos;
uniform float fillAngle;
uniform float2 fillCenter;
uniform float2 fillFocal;
uniform float fillScale;

uniform int strokeEnabled;
uniform int strokeAntialias;
uniform float strokeWidth;
uniform float strokeOutside;
uniform float strokeInside;
uniform int strokeType;
uniform int strokeGradientType;
uniform int strokeGradientSpread;
uniform float4 strokeColor;
uniform float4 strokeStartColor;
uniform float4 strokeEndColor;
uniform float strokeStartPos;
uniform float strokeEndPos;
uniform float strokeAngle;
uniform float2 strokeCenter;
uniform float2 strokeFocal;
uniform float strokeScale;

sampler_state atlasSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};
sampler_state fillGradientSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};
sampler_state strokeGradientSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float2 localPos : TEXCOORD1;
    float2 opacityData : TEXCOORD2;
    float4 overrideColor : TEXCOORD3;
    float4 animatorData : TEXCOORD4;
    float2 atlasScaleData : TEXCOORD5;
};
struct VertDataOut {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float2 localPos : TEXCOORD1;
    float opacity : TEXCOORD2;
    float4 overrideColor : TEXCOORD3;
    float4 animatorData : TEXCOORD4;
    float atlasPixelsPerLogical : TEXCOORD5;
};
VertDataOut VSDefault(VertDataIn v)
{
    VertDataOut o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.uv = v.uv;
    o.localPos = v.localPos;
    o.opacity = v.opacityData.x;
    o.overrideColor = v.overrideColor;
    o.animatorData = v.animatorData;
    o.atlasPixelsPerLogical = v.atlasScaleData.x;
    return o;
}

float spreadValue(float value, int mode)
{
    if (mode == 1)
        return value - floor(value);
    if (mode == 2) {
        float repeated = value - floor(value * 0.5) * 2.0;
        return repeated <= 1.0 ? repeated : 2.0 - repeated;
    }
    return clamp(value, 0.0, 1.0);
}

float gradientPosition(float2 samplePoint, int type, int spreadMode,
                       float angleDegrees, float2 centerNormalized,
                       float2 focalNormalized, float scaleValue)
{
    samplePoint -= materialOrigin;
    float2 size = max(materialSize, float2(1.0, 1.0));
    float2 center = centerNormalized * size;
    float2 focal = focalNormalized * size;
    float safeScale = clamp(scaleValue, 0.01, 100.0);
    float angle = angleDegrees * 0.017453292519943295;
    float value = 0.0;
    if (type == 1) {
        float radius = max(size.x, size.y) * 0.5 * safeScale;
        float focalDistance = length(center - focal);
        float effectiveRadius = max(1.0, radius - min(focalDistance, radius * 0.95));
        value = length(samplePoint - focal) / effectiveRadius;
    } else if (type == 2) {
        float a = atan2(samplePoint.y - center.y, samplePoint.x - center.x);
        value = (-a - angle) / 6.283185307179586 + 0.5;
    } else {
        float lengthValue = max(1.0, length(size) * 0.5 * safeScale);
        float2 direction = float2(cos(angle), sin(angle));
        value = dot(samplePoint - (center - direction * lengthValue), direction) /
                (lengthValue * 2.0);
    }
    return spreadValue(value, spreadMode);
}

float4 gradientColor(float2 samplePoint, int type, int gradientType,
                     int spreadMode, float4 solidColor,
                     float4 startColor, float4 endColor,
                     float startPos, float endPos, float angle,
                     float2 center, float2 focal, float scaleValue)
{
    if (type == 0)
        return solidColor;
    float position = gradientPosition(samplePoint, gradientType, spreadMode,
                                      angle, center, focal, scaleValue);
    float denominator = max(abs(endPos - startPos), 0.000001);
    float mixValue = clamp((position - startPos) / denominator, 0.0, 1.0);
    if (endPos < startPos)
        mixValue = 1.0 - mixValue;
    return lerp(startColor, endColor, mixValue);
}

float4 premultiplied(float4 color, float coverage)
{
    float alpha = clamp(color.a * coverage, 0.0, 1.0);
    return float4(color.rgb * alpha, alpha);
}
float coverageInside(float distanceValue, float aa)
{
    return smoothstep(-aa, aa, distanceValue);
}
float signedDistanceAt(float2 uv)
{
    return (glyphAtlas.Sample(atlasSampler, uv).r - 0.5) *
           (2.0 * sdfSpread);
}
float4 PSText(VertDataOut v) : TARGET
{
    float signedDistance = coverageMode != 0
        ? sdfSpread : signedDistanceAt(v.uv);
    float aa = coverageMode != 0 ? 0.5 :
        max(fwidth(signedDistance), 0.55);
    float coverageScale = max(v.atlasPixelsPerLogical, 0.125);
    float blurRadius = min(max(0.0, v.animatorData.y) * coverageScale,
                           max(0.0, sdfSpread * coverageScale - 2.0));
    /* The previous 13-tap fill plus 13-tap stroke implementation caused the
     * D3D shader optimiser shipped with OBS 32.2.1 to consume gigabytes and
     * hold the global graphics context for minutes. SDF blur is represented by
     * widening the analytic transition instead: one atlas sample, stable
     * compile time, and continuous animator blur without a shutdown-blocking
     * compiler job. */
    float coverageAa = aa + blurRadius * 0.5;
    float fillCoverage = coverageMode != 0
        ? 1.0
        : coverageInside(signedDistance, coverageAa);

    float strokeCoverage = 0.0;
    if (coverageMode == 0 && strokeEnabled != 0 && strokeWidth > 0.0001) {
        float strokeAa = strokeAntialias != 0 ? coverageAa : 0.0001;
        /* Text-only placement contract: outer coverage expands the SDF edge,
         * inner coverage consumes the glyph interior, and mid splits exactly. */
        strokeCoverage = clamp(
            coverageInside(signedDistance + strokeOutside + v.animatorData.z, strokeAa) -
            coverageInside(signedDistance - strokeInside - v.animatorData.w, strokeAa),
            0.0, 1.0);
    }

    float fillPosition = gradientPosition(
        v.localPos, fillGradientType, fillGradientSpread,
        fillAngle, fillCenter, fillFocal, fillScale);
    float strokePosition = gradientPosition(
        v.localPos, strokeGradientType, strokeGradientSpread,
        strokeAngle, strokeCenter, strokeFocal, strokeScale);
    float4 fillMaterial = fillType == 0
        ? fillColor
        : fillGradientLut.Sample(fillGradientSampler,
                                 float2(fillPosition, 0.5));
    float4 strokeMaterial = strokeType == 0
        ? strokeColor
        : strokeGradientLut.Sample(strokeGradientSampler,
                                   float2(strokePosition, 0.5));

    if (drawPart != 0) {
        strokeMaterial = lerp(strokeMaterial, v.overrideColor,
                              clamp(v.animatorData.x, 0.0, 1.0));
        return premultiplied(strokeMaterial, strokeCoverage * v.opacity);
    }
    fillMaterial = lerp(fillMaterial, v.overrideColor,
                        clamp(v.animatorData.x, 0.0, 1.0));
    return premultiplied(fillMaterial, fillCoverage * v.opacity);
}

technique Draw {
    pass {
        vertex_shader = VSDefault(v);
        pixel_shader = PSText(v);
    }
}
)";

struct GlyphKey {
    uint64_t font = 0;
    uint64_t variant = 0;
    uint32_t glyph = 0;

    bool operator==(const GlyphKey &other) const
    {
        return font == other.font && variant == other.variant &&
               glyph == other.glyph;
    }
};

struct GlyphKeyHash {
    size_t operator()(const GlyphKey &key) const
    {
        uint64_t value = key.font;
        value ^= key.variant + 0x9e3779b97f4a7c15ULL +
                 (value << 6) + (value >> 2);
        value ^= static_cast<uint64_t>(key.glyph) +
                 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
        return static_cast<size_t>(value ^ (value >> 32));
    }
};

struct AtlasGlyph {
    int page = -1;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    /* Baseline-relative top-left of the scaled glyph coverage, before the SDF
     * padding is added. Keeping this in the atlas entry avoids guessing the
     * alpha-map bearing and guarantees that ink and advances share one origin. */
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float atlas_pixels_per_logical = 1.0f;
    float padding_logical = static_cast<float>(kSdfSpread + 2);
};

struct AtlasPage {
    std::vector<uint8_t> pixels;
    int cursor_x = kAtlasGap;
    int cursor_y = kAtlasGap;
    int row_height = 0;
    bool dirty = true;
    int dirty_x0 = kAtlasSize;
    int dirty_y0 = kAtlasSize;
    int dirty_x1 = 0;
    int dirty_y1 = 0;
    gs_texture_t *texture = nullptr;

    AtlasPage() : pixels(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0) {}

    void mark_dirty(int x, int y, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;
        dirty = true;
        dirty_x0 = std::min(dirty_x0, x);
        dirty_y0 = std::min(dirty_y0, y);
        dirty_x1 = std::max(dirty_x1, x + width);
        dirty_y1 = std::max(dirty_y1, y + height);
    }

    void clear_dirty()
    {
        dirty = false;
        dirty_x0 = dirty_y0 = kAtlasSize;
        dirty_x1 = dirty_y1 = 0;
    }
};

struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float local_x = 0.0f;
    float local_y = 0.0f;
    float opacity = 1.0f;
    float override_r = 1.0f;
    float override_g = 1.0f;
    float override_b = 1.0f;
    float override_a = 1.0f;
    float color_mix = 0.0f;
    float blur = 0.0f;
    float stroke_outside_delta = 0.0f;
    float stroke_inside_delta = 0.0f;
    float atlas_pixels_per_logical = 1.0f;
};

struct Material {
    RichTextFill fill;
    RichTextStroke stroke;
};

enum class DrawPart : uint8_t {
    BehindStroke = 0,
    Fill = 1,
    FrontStroke = 2,
};

struct MaterialDomain {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct Batch {
    int page = -1;
    size_t paint_index = 0;
    bool solid_geometry = false;
    DrawPart draw_part = DrawPart::Fill;
    Material material;
    MaterialDomain domain;
    std::vector<Vertex> vertices;
};

struct Quad {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float local_x0 = 0.0f;
    float local_y0 = 0.0f;
    float local_x1 = 0.0f;
    float local_y1 = 0.0f;
    float atlas_pixels_per_logical = 1.0f;
};

static void hash_bytes(uint64_t &hash, const QByteArray &bytes)
{
    for (unsigned char value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
}

static uint64_t raw_font_fingerprint(const QRawFont &font)
{
    const std::string family = font.familyName().toStdString();
    const std::string style = font.styleName().toStdString();
    const float pixel_size = static_cast<float>(font.pixelSize());
    uint64_t hash = 1469598103934665603ULL;
    hash_bytes(hash, QByteArray::fromStdString(family));
    hash_bytes(hash, QByteArray("\n", 1));
    hash_bytes(hash, QByteArray::fromStdString(style));
    hash_bytes(hash, font.fontTable("head"));
    hash_bytes(hash, font.fontTable("maxp"));
    hash_bytes(hash, font.fontTable("name"));
    uint32_t size_bits = 0;
    static_assert(sizeof(size_bits) == sizeof(pixel_size), "float size");
    std::memcpy(&size_bits, &pixel_size, sizeof(size_bits));
    hash ^= size_bits;
    hash *= 1099511628211ULL;
    return hash;
}

static uint32_t quantized_glyph_scale(float value)
{
    const double clamped = std::clamp(static_cast<double>(value), 0.01, 100.0);
    return static_cast<uint32_t>(std::llround(clamped * 10000.0));
}

static uint64_t shaping_variant_fingerprint(
    const TextLayoutShapingStyle &style, float scale_x, float scale_y,
    float atlas_pixels_per_logical)
{
    uint64_t hash = 1469598103934665603ULL;
    auto add_u32 = [&](uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint8_t>((value >> shift) & 0xFFu);
            hash *= 1099511628211ULL;
        }
    };
    add_u32(style.bold ? 1u : 0u);
    add_u32(style.italic ? 1u : 0u);
    add_u32(static_cast<uint32_t>(style.text_style));
    /* v277: H/V Scale is baked into the actual glyph coverage. This makes the
     * atlas bitmap, the visible glyph and the immutable selection geometry use
     * the same anisotropic transform on every graphics backend. Quantization is
     * fine enough for property editing while bounding animated atlas variants. */
    add_u32(quantized_glyph_scale(scale_x));
    add_u32(quantized_glyph_scale(scale_y));
    add_u32(quantized_glyph_scale(atlas_pixels_per_logical));
    return hash;
}

static float glyph_atlas_coverage_scale(const TextLayoutRun &run,
                                        float scale_x, float scale_y,
                                        float preview_scale)
{
    float requested = std::clamp(preview_scale, 0.25f, 1.0f);
    const float estimated_extent = std::max(
        1.0f, run.font.pixel_size * std::max(scale_x, scale_y));
    if (estimated_extent > 512.0f)
        requested = std::min(requested, 512.0f / estimated_extent);
    const int raster_spread = std::clamp(
        static_cast<int>(std::lround(kSdfSpread * requested)), 4, kSdfSpread);
    return static_cast<float>(raster_spread) /
           static_cast<float>(kSdfSpread);
}

static QRawFont reconstructed_raw_font_for_run(const TextLayoutRun &run)
{
    const TextLayoutFontKey &key = run.font;
    const int pixel_size =
        std::max(1, static_cast<int>(std::lround(key.pixel_size)));
    QFontDatabase database;
    const QString family = QString::fromStdString(key.family);
    const QString requested_style = QString::fromStdString(key.style);

    auto exact_raw = [&](QFont font) -> QRawFont {
        font.setPixelSize(pixel_size);
        QRawFont raw = QRawFont::fromFont(font);
        if (!raw.isValid())
            return {};
        raw.setPixelSize(key.pixel_size);
        return raw_font_fingerprint(raw) == key.fingerprint ? raw : QRawFont{};
    };

    /* The layout key already describes the physical face returned by Qt.
     * Reapplying synthetic bold/italic/stretch here can make Fontconfig choose
     * a different face on Linux, so try the exact family/style first. */
    if (!family.isEmpty()) {
        if (QRawFont raw = exact_raw(database.font(
                family, requested_style, pixel_size)); raw.isValid())
            return raw;

        QFont direct(family);
        if (!requested_style.isEmpty())
            direct.setStyleName(requested_style);
        if (QRawFont raw = exact_raw(direct); raw.isValid())
            return raw;

        /* Style display names vary across Fontconfig backends. Enumerating the
         * installed family is a bounded compatibility fallback and happens at
         * most once per physical face because Renderer::Impl caches success. */
        const QStringList styles = database.styles(family);
        for (const QString &style : styles) {
            if (QRawFont raw = exact_raw(database.font(
                    family, style, pixel_size)); raw.isValid())
                return raw;
        }
    }
    return {};
}

static bool raw_font_has_color_glyph_tables(const QRawFont &font)
{
    return !font.fontTable("COLR").isEmpty() ||
           !font.fontTable("CBDT").isEmpty() ||
           !font.fontTable("sbix").isEmpty() ||
           !font.fontTable("SVG ").isEmpty();
}

/* QRawFont::alphaMapForGlyph() is backend-dependent. DirectWrite commonly
 * returns a luminance image, while FreeType/Fontconfig commonly returns
 * QImage::Format_Alpha8. Converting Alpha8 with convertToFormat(Grayscale8)
 * does not preserve the alpha plane as glyph coverage on every Qt build and
 * can therefore turn valid Linux glyphs into an all-black SDF input. Extract
 * the actual coverage channel explicitly and keep the atlas input contract
 * identical on Windows, Linux and macOS. */
static QImage glyph_coverage_grayscale8(const QImage &source)
{
    if (source.isNull() || source.width() <= 0 || source.height() <= 0)
        return {};

    QImage coverage(source.size(), QImage::Format_Grayscale8);
    if (coverage.isNull())
        return {};

    if (source.format() == QImage::Format_Alpha8 ||
        source.format() == QImage::Format_Grayscale8) {
        for (int y = 0; y < source.height(); ++y) {
            std::memcpy(coverage.scanLine(y), source.constScanLine(y),
                        static_cast<size_t>(source.width()));
        }
        return coverage;
    }

    const bool use_alpha = source.hasAlphaChannel();
    const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
    if (argb.isNull())
        return {};
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *input = reinterpret_cast<const QRgb *>(
            argb.constScanLine(y));
        uchar *output = coverage.scanLine(y);
        for (int x = 0; x < argb.width(); ++x)
            output[x] = static_cast<uchar>(
                use_alpha ? qAlpha(input[x]) : qGray(input[x]));
    }
    return coverage;
}

static uint32_t multiply_alpha(uint32_t argb, float multiplier)
{
    const uint32_t alpha = (argb >> 24) & 0xFFu;
    const uint32_t resolved = static_cast<uint32_t>(std::lround(
        std::clamp(multiplier, 0.0f, 1.0f) * static_cast<float>(alpha)));
    return (argb & 0x00FFFFFFu) | (std::min(255u, resolved) << 24);
}

static void crop_quad_padding(Quad &quad, float inset_x, float inset_y)
{
    if ((inset_x <= 0.0f && inset_y <= 0.0f) ||
        quad.x1 <= quad.x0 || quad.y1 <= quad.y0)
        return;
    const float width = quad.x1 - quad.x0;
    const float height = quad.y1 - quad.y0;
    const float resolved_x = std::clamp(
        inset_x, 0.0f, std::max(0.0f, width * 0.5f - 0.001f));
    const float resolved_y = std::clamp(
        inset_y, 0.0f, std::max(0.0f, height * 0.5f - 0.001f));
    if (resolved_x <= 0.0f && resolved_y <= 0.0f)
        return;

    const float tx = resolved_x / width;
    const float ty = resolved_y / height;
    const float old_u0 = quad.u0;
    const float old_v0 = quad.v0;
    const float old_u1 = quad.u1;
    const float old_v1 = quad.v1;
    const float old_lx0 = quad.local_x0;
    const float old_ly0 = quad.local_y0;
    const float old_lx1 = quad.local_x1;
    const float old_ly1 = quad.local_y1;

    quad.x0 += resolved_x;
    quad.y0 += resolved_y;
    quad.x1 -= resolved_x;
    quad.y1 -= resolved_y;
    quad.u0 = old_u0 + (old_u1 - old_u0) * tx;
    quad.v0 = old_v0 + (old_v1 - old_v0) * ty;
    quad.u1 = old_u1 - (old_u1 - old_u0) * tx;
    quad.v1 = old_v1 - (old_v1 - old_v0) * ty;
    quad.local_x0 = old_lx0 + (old_lx1 - old_lx0) * tx;
    quad.local_y0 = old_ly0 + (old_ly1 - old_ly0) * ty;
    quad.local_x1 = old_lx1 - (old_lx1 - old_lx0) * tx;
    quad.local_y1 = old_ly1 - (old_ly1 - old_ly0) * ty;
}

static bool clip_quad(Quad &quad, float clip_x0, float clip_y0,
                      float clip_x1, float clip_y1)
{
    if (quad.x1 <= quad.x0 || quad.y1 <= quad.y0)
        return false;
    const float cx0 = std::max(quad.x0, clip_x0);
    const float cy0 = std::max(quad.y0, clip_y0);
    const float cx1 = std::min(quad.x1, clip_x1);
    const float cy1 = std::min(quad.y1, clip_y1);
    if (cx1 <= cx0 || cy1 <= cy0)
        return false;

    const float tx0 = (cx0 - quad.x0) / (quad.x1 - quad.x0);
    const float ty0 = (cy0 - quad.y0) / (quad.y1 - quad.y0);
    const float tx1 = (cx1 - quad.x0) / (quad.x1 - quad.x0);
    const float ty1 = (cy1 - quad.y0) / (quad.y1 - quad.y0);
    const float original_u0 = quad.u0;
    const float original_v0 = quad.v0;
    const float original_u1 = quad.u1;
    const float original_v1 = quad.v1;
    const float original_local_x0 = quad.local_x0;
    const float original_local_y0 = quad.local_y0;
    const float original_local_x1 = quad.local_x1;
    const float original_local_y1 = quad.local_y1;

    quad.u0 = original_u0 + (original_u1 - original_u0) * tx0;
    quad.v0 = original_v0 + (original_v1 - original_v0) * ty0;
    quad.u1 = original_u0 + (original_u1 - original_u0) * tx1;
    quad.v1 = original_v0 + (original_v1 - original_v0) * ty1;
    quad.local_x0 = original_local_x0 +
                    (original_local_x1 - original_local_x0) * tx0;
    quad.local_y0 = original_local_y0 +
                    (original_local_y1 - original_local_y0) * ty0;
    quad.local_x1 = original_local_x0 +
                    (original_local_x1 - original_local_x0) * tx1;
    quad.local_y1 = original_local_y0 +
                    (original_local_y1 - original_local_y0) * ty1;
    quad.x0 = cx0;
    quad.y0 = cy0;
    quad.x1 = cx1;
    quad.y1 = cy1;
    return true;
}

static void append_quad(std::vector<Vertex> &vertices, const Quad &source,
                        const PrepareOptions &options,
                        const TextAnimatorClusterState *animation = nullptr,
                        float opacity_multiplier = 1.0f,
                        bool stroke_part = false,
                        float stroke_outside_delta = 0.0f,
                        float stroke_inside_delta = 0.0f)
{
    Quad quad = source;
    if (animation && animation->has_reveal_bounds &&
        animation->reveal_direction != TextRevealDirection::None) {
        const float reveal = static_cast<float>(std::clamp(
            animation->reveal, 0.0, 1.0));
        const float margin = 1.5f;
        float rx0 = options.text_offset_x +
                    static_cast<float>(animation->reveal_x0) - margin;
        float ry0 = options.text_offset_y +
                    static_cast<float>(animation->reveal_y0) - margin;
        float rx1 = options.text_offset_x +
                    static_cast<float>(animation->reveal_x1) + margin;
        float ry1 = options.text_offset_y +
                    static_cast<float>(animation->reveal_y1) + margin;
        switch (animation->reveal_direction) {
        case TextRevealDirection::Right:
            rx0 = rx1 - (rx1 - rx0) * reveal;
            break;
        case TextRevealDirection::Up:
            ry0 = ry1 - (ry1 - ry0) * reveal;
            break;
        case TextRevealDirection::Down:
            ry1 = ry0 + (ry1 - ry0) * reveal;
            break;
        case TextRevealDirection::Left:
            rx1 = rx0 + (rx1 - rx0) * reveal;
            break;
        case TextRevealDirection::None:
            break;
        }
        if (!clip_quad(quad, rx0, ry0, rx1, ry1))
            return;
    }
    const bool transformed = animation &&
        (std::abs(animation->position_x) > 1e-9 ||
         std::abs(animation->position_y) > 1e-9 ||
         std::abs(animation->scale_x * animation->horizontal_scale - 1.0) > 1e-9 ||
         std::abs(animation->scale_y * animation->vertical_scale - 1.0) > 1e-9 ||
         std::abs(animation->rotation + animation->character_rotation) > 1e-9 ||
         std::abs(animation->anchor_x) > 1e-9 ||
         std::abs(animation->anchor_y) > 1e-9 ||
         std::abs(animation->skew) > 1e-9);
    if (!transformed && !clip_quad(quad, options.clip_x, options.clip_y,
                                   options.clip_x + options.clip_width,
                                   options.clip_y + options.clip_height))
        return;

    const float scale = std::clamp(options.raster_scale, 0.01f, 8.0f);
    float final_opacity = std::clamp(opacity_multiplier, 0.0f, 1.0f);
    if (animation) {
        const double reveal_opacity =
            animation->reveal_direction == TextRevealDirection::None
                ? animation->reveal : 1.0;
        final_opacity *= static_cast<float>(std::clamp(
            animation->opacity * animation->visibility * reveal_opacity,
            0.0, 1.0));
    }

    const double base_center_x = animation && animation->has_transform_origin
        ? options.text_offset_x + animation->transform_origin_x
        : (quad.x0 + quad.x1) * 0.5;
    const double base_center_y = animation && animation->has_transform_origin
        ? options.text_offset_y + animation->transform_origin_y
        : (quad.y0 + quad.y1) * 0.5;
    const double center_x = base_center_x +
                            (animation ? animation->anchor_x : 0.0);
    const double center_y = base_center_y +
                            (animation ? animation->anchor_y : 0.0);
    const double sx = animation ? animation->scale_x * animation->horizontal_scale : 1.0;
    const double sy = animation ? animation->scale_y * animation->vertical_scale : 1.0;
    const double radians = animation
        ? (animation->rotation + animation->character_rotation) * kPi / 180.0
        : 0.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double tx = animation ? animation->position_x : 0.0;
    const double ty = animation ? animation->position_y - animation->baseline_shift : 0.0;
    const double skew_radians = animation ? animation->skew * kPi / 180.0 : 0.0;
    const double skew_axis_radians = animation ? animation->skew_axis * kPi / 180.0 : 0.0;
    const double axis_cosine = std::cos(skew_axis_radians);
    const double axis_sine = std::sin(skew_axis_radians);
    const double shear = std::tan(std::clamp(skew_radians, -1.553343, 1.553343));
    auto point = [&](double x, double y) {
        double local_x = (x - center_x) * sx;
        double local_y = (y - center_y) * sy;
        if (std::abs(shear) > 1.0e-12) {
            const double axis_x = local_x * axis_cosine + local_y * axis_sine;
            const double axis_y = -local_x * axis_sine + local_y * axis_cosine;
            const double skewed_x = axis_x + shear * axis_y;
            local_x = skewed_x * axis_cosine - axis_y * axis_sine;
            local_y = skewed_x * axis_sine + axis_y * axis_cosine;
        }
        return std::pair<float, float>{
            static_cast<float>((center_x + local_x * cosine - local_y * sine + tx) * scale),
            static_cast<float>((center_y + local_x * sine + local_y * cosine + ty) * scale)};
    };
    const auto p0 = point(quad.x0, quad.y0);
    const auto p1 = point(quad.x1, quad.y0);
    const auto p2 = point(quad.x1, quad.y1);
    const auto p3 = point(quad.x0, quad.y1);
    uint32_t override_argb = 0xFFFFFFFFu;
    float color_mix = 0.0f;
    float blur = 0.0f;
    if (animation) {
        override_argb = stroke_part ? animation->stroke_color
                                    : animation->fill_color;
        color_mix = static_cast<float>(std::clamp(
            stroke_part ? animation->stroke_color_mix
                        : animation->fill_color_mix, 0.0, 1.0));
        blur = static_cast<float>(std::max(0.0, animation->blur));
    }
    const float override_r = static_cast<float>((override_argb >> 16) & 0xFF) / 255.0f;
    const float override_g = static_cast<float>((override_argb >> 8) & 0xFF) / 255.0f;
    const float override_b = static_cast<float>(override_argb & 0xFF) / 255.0f;
    const float override_a = static_cast<float>((override_argb >> 24) & 0xFF) / 255.0f;
    const Vertex a{p0.first, p0.second, quad.u0, quad.v0,
                   quad.local_x0, quad.local_y0, final_opacity,
                   override_r, override_g, override_b, override_a,
                   color_mix, blur, stroke_outside_delta, stroke_inside_delta,
                   quad.atlas_pixels_per_logical};
    const Vertex b{p1.first, p1.second, quad.u1, quad.v0,
                   quad.local_x1, quad.local_y0, final_opacity,
                   override_r, override_g, override_b, override_a,
                   color_mix, blur, stroke_outside_delta, stroke_inside_delta,
                   quad.atlas_pixels_per_logical};
    const Vertex c{p2.first, p2.second, quad.u1, quad.v1,
                   quad.local_x1, quad.local_y1, final_opacity,
                   override_r, override_g, override_b, override_a,
                   color_mix, blur, stroke_outside_delta, stroke_inside_delta,
                   quad.atlas_pixels_per_logical};
    const Vertex d{p3.first, p3.second, quad.u0, quad.v1,
                   quad.local_x0, quad.local_y1, final_opacity,
                   override_r, override_g, override_b, override_a,
                   color_mix, blur, stroke_outside_delta, stroke_inside_delta,
                   quad.atlas_pixels_per_logical};
    vertices.reserve(vertices.size() + 6);
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
    vertices.push_back(a); vertices.push_back(c); vertices.push_back(d);
}

static void append_quad(std::vector<Vertex> &vertices,
                        float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1,
                        float local_x0, float local_y0,
                        float local_x1, float local_y1,
                        const PrepareOptions &options)
{
    append_quad(vertices,
                Quad{x0, y0, x1, y1, u0, v0, u1, v1,
                     local_x0, local_y0, local_x1, local_y1},
                options, nullptr, 1.0f);
}

static size_t paint_run_index_at(const std::vector<TextLayoutPaintRun> &runs,
                                 size_t byte_offset)
{
    if (runs.empty())
        return 0;
    const auto upper = std::upper_bound(
        runs.begin(), runs.end(), byte_offset,
        [](size_t value, const TextLayoutPaintRun &run) {
            return value < run.byte_start;
        });
    if (upper == runs.begin())
        return 0;
    const auto candidate = std::prev(upper);
    const size_t index = static_cast<size_t>(candidate - runs.begin());
    const size_t end = candidate->byte_start + candidate->byte_length;
    if (byte_offset < end || upper == runs.end())
        return index;
    return static_cast<size_t>(upper - runs.begin());
}

static gs_vertbuffer_t *create_vertex_buffer(const std::vector<Vertex> &vertices)
{
    if (vertices.empty())
        return nullptr;
    gs_vb_data *data = gs_vbdata_create();
    if (!data)
        return nullptr;
    data->num = vertices.size();
    data->points = static_cast<vec3 *>(bzalloc(sizeof(vec3) * data->num));
    data->num_tex = 6;
    data->tvarray = static_cast<gs_tvertarray *>(
        bzalloc(sizeof(gs_tvertarray) * data->num_tex));
    if (!data->points || !data->tvarray) {
        gs_vbdata_destroy(data);
        return nullptr;
    }
    data->tvarray[0].width = 2;
    data->tvarray[0].array = bzalloc(sizeof(vec2) * data->num);
    data->tvarray[1].width = 2;
    data->tvarray[1].array = bzalloc(sizeof(vec2) * data->num);
    // OBS' D3D11 backend accepts 2- or 4-component texture vertex streams.
    // Pack scalar values into vec2 streams and consume their x component in
    // the vertex shader so GPU text does not fall back to the CPU renderer.
    data->tvarray[2].width = 2;
    data->tvarray[2].array = bzalloc(sizeof(vec2) * data->num);
    data->tvarray[3].width = 4;
    data->tvarray[3].array = bzalloc(sizeof(vec4) * data->num);
    data->tvarray[4].width = 4;
    data->tvarray[4].array = bzalloc(sizeof(vec4) * data->num);
    data->tvarray[5].width = 2;
    data->tvarray[5].array = bzalloc(sizeof(vec2) * data->num);
    if (!data->tvarray[0].array || !data->tvarray[1].array ||
        !data->tvarray[2].array || !data->tvarray[3].array ||
        !data->tvarray[4].array || !data->tvarray[5].array) {
        gs_vbdata_destroy(data);
        return nullptr;
    }
    auto *uv = static_cast<vec2 *>(data->tvarray[0].array);
    auto *local = static_cast<vec2 *>(data->tvarray[1].array);
    auto *opacity = static_cast<vec2 *>(data->tvarray[2].array);
    auto *override_color = static_cast<vec4 *>(data->tvarray[3].array);
    auto *animator_data = static_cast<vec4 *>(data->tvarray[4].array);
    auto *atlas_pixels_per_logical =
        static_cast<vec2 *>(data->tvarray[5].array);
    for (size_t i = 0; i < vertices.size(); ++i) {
        vec3_set(&data->points[i], vertices[i].x, vertices[i].y, 0.0f);
        vec2_set(&uv[i], vertices[i].u, vertices[i].v);
        vec2_set(&local[i], vertices[i].local_x, vertices[i].local_y);
        vec2_set(&opacity[i], vertices[i].opacity, 0.0f);
        vec4_set(&override_color[i], vertices[i].override_r,
                 vertices[i].override_g, vertices[i].override_b,
                 vertices[i].override_a);
        vec4_set(&animator_data[i], vertices[i].color_mix,
                 vertices[i].blur, vertices[i].stroke_outside_delta,
                 vertices[i].stroke_inside_delta);
        vec2_set(&atlas_pixels_per_logical[i],
                 vertices[i].atlas_pixels_per_logical, 0.0f);
    }
    return gs_vertexbuffer_create(data, 0);
}

static void set_vec2(gs_effect_t *effect, const char *name, float x, float y)
{
    if (gs_eparam_t *param = gpu_text_effect_param(effect, name)) {
        vec2 value;
        vec2_set(&value, x, y);
        gs_effect_set_vec2(param, &value);
    }
}

static void set_color(gs_effect_t *effect, const char *name, uint32_t argb)
{
    if (gs_eparam_t *param = gpu_text_effect_param(effect, name)) {
        vec4 value;
        vec4_set(&value,
                 static_cast<float>((argb >> 16) & 0xFF) / 255.0f,
                 static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                 static_cast<float>(argb & 0xFF) / 255.0f,
                 static_cast<float>((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(param, &value);
    }
}

static void set_int(gs_effect_t *effect, const char *name, int value)
{
    if (gs_eparam_t *param = gpu_text_effect_param(effect, name))
        gs_effect_set_int(param, value);
}

static void set_float(gs_effect_t *effect, const char *name, float value)
{
    if (gs_eparam_t *param = gpu_text_effect_param(effect, name))
        gs_effect_set_float(param, value);
}

static int normalized_gradient_type(int gradient_type)
{
    switch (std::clamp(gradient_type, 0, 4)) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 4:
        return 1;
    case 0:
    case 3:
    default:
        return 0;
    }
}

static int normalized_gradient_spread(int gradient_spread, int gradient_type)
{
    if (gradient_spread == 1 || gradient_spread == 2)
        return gradient_spread;
    return std::clamp(gradient_type, 0, 4) == 3 ? 1 : 0;
}

struct GradientLutStop {
    float position = 0.0f;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
};

static GradientLutStop gradient_lut_stop(uint32_t argb, float position,
                                         float stop_opacity,
                                         float opacity_multiplier)
{
    GradientLutStop stop;
    stop.position = std::clamp(position, 0.0f, 1.0f);
    stop.red = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    stop.green = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    stop.blue = static_cast<float>(argb & 0xFF) / 255.0f;
    stop.alpha = std::clamp(
        static_cast<float>((argb >> 24) & 0xFF) / 255.0f *
            stop_opacity * opacity_multiplier,
        0.0f, 1.0f);
    return stop;
}

static std::array<uint8_t, kGradientLutWidth * 4> gradient_lut_pixels(
    const RichTextFill &fill, float opacity_multiplier)
{
    std::vector<GradientLutStop> stops;
    stops.reserve(fill.gradient_stops.size() + 2);
    const float overall_opacity = std::clamp(
        opacity_multiplier * fill.gradient_opacity, 0.0f, 1.0f);
    stops.push_back(gradient_lut_stop(
        fill.gradient_start_color, fill.gradient_start_pos,
        fill.gradient_start_opacity, overall_opacity));
    stops.push_back(gradient_lut_stop(
        fill.gradient_end_color, fill.gradient_end_pos,
        fill.gradient_end_opacity, overall_opacity));
    for (const GradientStop &extra : fill.gradient_stops) {
        stops.push_back(gradient_lut_stop(
            extra.color, extra.position, extra.opacity, overall_opacity));
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const GradientLutStop &left,
                        const GradientLutStop &right) {
                         return left.position < right.position;
                     });

    std::array<uint8_t, kGradientLutWidth * 4> pixels{};
    auto channel = [](float value) {
        return static_cast<uint8_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    size_t right_index = 0;
    for (uint32_t x = 0; x < kGradientLutWidth; ++x) {
        const float position = static_cast<float>(x) /
                               static_cast<float>(kGradientLutWidth - 1);
        while (right_index < stops.size() &&
               stops[right_index].position < position)
            ++right_index;
        const GradientLutStop &right =
            right_index < stops.size() ? stops[right_index] : stops.back();
        const GradientLutStop &left = right_index == 0
            ? stops.front() : stops[right_index - 1];
        const float span = right.position - left.position;
        const float mix_value = span > 0.000001f
            ? std::clamp((position - left.position) / span, 0.0f, 1.0f)
            : (position >= right.position ? 1.0f : 0.0f);
        const auto mix = [mix_value](float a, float b) {
            return a + (b - a) * mix_value;
        };
        const size_t offset = static_cast<size_t>(x) * 4;
        pixels[offset + 0] = channel(mix(left.blue, right.blue));
        pixels[offset + 1] = channel(mix(left.green, right.green));
        pixels[offset + 2] = channel(mix(left.red, right.red));
        pixels[offset + 3] = channel(mix(left.alpha, right.alpha));
    }
    return pixels;
}

static bool upload_gradient_lut(gs_texture_t *&texture,
                                const RichTextFill &fill,
                                float opacity_multiplier)
{
    const auto pixels = gradient_lut_pixels(fill, opacity_multiplier);
    if (!texture) {
        const uint8_t *planes[1] = {pixels.data()};
        texture = gs_texture_create(kGradientLutWidth, 1, GS_BGRA, 1,
                                    planes, GS_DYNAMIC);
    } else {
        gs_texture_set_image(texture, pixels.data(),
                             kGradientLutWidth * 4, false);
    }
    return texture != nullptr;
}

static void set_fill_params(gs_effect_t *effect, const char *prefix,
                            const RichTextFill &fill, float opacity_multiplier)
{
    const std::string type_name = std::string(prefix) + "Type";
    const std::string gradient_type_name = std::string(prefix) + "GradientType";
    const std::string spread_name = std::string(prefix) + "GradientSpread";
    const std::string color_name = std::string(prefix) + "Color";
    const std::string start_color_name = std::string(prefix) + "StartColor";
    const std::string end_color_name = std::string(prefix) + "EndColor";
    const std::string start_pos_name = std::string(prefix) + "StartPos";
    const std::string end_pos_name = std::string(prefix) + "EndPos";
    const std::string angle_name = std::string(prefix) + "Angle";
    const std::string center_name = std::string(prefix) + "Center";
    const std::string focal_name = std::string(prefix) + "Focal";
    const std::string scale_name = std::string(prefix) + "Scale";

    set_int(effect, type_name.c_str(), fill.type == 1 ? 1 : 0);
    set_int(effect, gradient_type_name.c_str(),
            normalized_gradient_type(fill.gradient_type));
    set_int(effect, spread_name.c_str(),
            normalized_gradient_spread(fill.gradient_spread,
                                       fill.gradient_type));
    set_color(effect, color_name.c_str(), multiply_alpha(fill.color, opacity_multiplier));
    set_color(effect, start_color_name.c_str(),
              multiply_alpha(fill.gradient_start_color,
                             opacity_multiplier * fill.gradient_opacity *
                                 fill.gradient_start_opacity));
    set_color(effect, end_color_name.c_str(),
              multiply_alpha(fill.gradient_end_color,
                             opacity_multiplier * fill.gradient_opacity *
                                 fill.gradient_end_opacity));
    set_float(effect, start_pos_name.c_str(),
              std::clamp(fill.gradient_start_pos, 0.0f, 1.0f));
    set_float(effect, end_pos_name.c_str(),
              std::clamp(fill.gradient_end_pos, 0.0f, 1.0f));
    set_float(effect, angle_name.c_str(), fill.gradient_angle);
    set_vec2(effect, center_name.c_str(), fill.gradient_center_x,
             fill.gradient_center_y);
    set_vec2(effect, focal_name.c_str(), fill.gradient_focal_x,
             fill.gradient_focal_y);
    set_float(effect, scale_name.c_str(),
              std::clamp(fill.gradient_scale, 0.01f, 100.0f));
}

} // namespace

struct Layer::Impl {
    PrepareOptions options;
    std::vector<Batch> batches;
    gs_texrender_t *targets[2] = {nullptr, nullptr};
    int active_target = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    bool pending = false;
};

struct Renderer::Impl {
    std::vector<AtlasPage> pages;
    std::unordered_map<GlyphKey, AtlasGlyph, GlyphKeyHash> glyphs;
    std::unordered_map<uint64_t, QRawFont> raw_fonts;
    gs_effect_t *effect = nullptr;
    gs_texture_t *white_texture = nullptr;
    gs_texture_t *fill_gradient_lut = nullptr;
    gs_texture_t *stroke_gradient_lut = nullptr;
    bool backend_available = true;
    std::string last_error;
    std::mutex effect_mutex;

    bool allocate_rect(int width, int height, AtlasGlyph &glyph)
    {
        if (width + kAtlasGap * 2 > kAtlasSize ||
            height + kAtlasGap * 2 > kAtlasSize)
            return false;
        for (size_t page_index = 0; page_index <= pages.size(); ++page_index) {
            if (page_index == pages.size()) {
                if (pages.size() >= kAtlasMaxPages)
                    return false;
                pages.emplace_back();
            }
            AtlasPage &page = pages[page_index];
            if (page.cursor_x + width + kAtlasGap > kAtlasSize) {
                page.cursor_x = kAtlasGap;
                page.cursor_y += page.row_height + kAtlasGap;
                page.row_height = 0;
            }
            if (page.cursor_y + height + kAtlasGap > kAtlasSize)
                continue;
            glyph.page = static_cast<int>(page_index);
            glyph.x = page.cursor_x;
            glyph.y = page.cursor_y;
            glyph.width = width;
            glyph.height = height;
            page.cursor_x += width + kAtlasGap;
            page.row_height = std::max(page.row_height, height);
            return true;
        }
        return false;
    }

    bool glyph_for(const TextLayoutRun &run, uint32_t glyph_id,
                   float scale_x, float scale_y, float preview_scale,
                   AtlasGlyph &out, std::string &reason)
    {
        const TextLayoutFontKey &font_key = run.font;
        const float resolved_scale_x = std::clamp(scale_x, 0.01f, 100.0f);
        const float resolved_scale_y = std::clamp(scale_y, 0.01f, 100.0f);
        const float coverage_scale = glyph_atlas_coverage_scale(
            run, resolved_scale_x, resolved_scale_y, preview_scale);
        const int raster_spread = std::clamp(
            static_cast<int>(std::lround(kSdfSpread * coverage_scale)),
            4, kSdfSpread);
        const GlyphKey key{font_key.fingerprint,
                           shaping_variant_fingerprint(
                               run.shaping_style, resolved_scale_x,
                               resolved_scale_y, coverage_scale),
                           glyph_id};
        const auto found = glyphs.find(key);
        if (found != glyphs.end()) {
            out = found->second;
            return true;
        }

        QRawFont raw;
        const auto cached_font = raw_fonts.find(font_key.fingerprint);
        if (cached_font != raw_fonts.end()) {
            raw = cached_font->second;
        } else {
            raw = text_layout_registered_raw_font(font_key);
            if (!raw.isValid())
                raw = reconstructed_raw_font_for_run(run);
            if (raw.isValid())
                raw_fonts.emplace(font_key.fingerprint, raw);
        }
        if (!raw.isValid()) {
            reason = "Could not reconstruct the exact shaped font face.";
            return false;
        }
        if (raw_font_has_color_glyph_tables(raw)) {
            reason = "Color-font glyphs require the compatibility raster path.";
            return false;
        }
        /* Scale the vector outline around its baseline origin before rasterizing
         * it. The scale values come from the exact TextLayoutGlyph cluster, not
         * from a coalesced QGlyphRun, so rich-text ranges remain independent. */
        QPainterPath glyph_path = raw.pathForGlyph(glyph_id);
        if (!glyph_path.isEmpty()) {
            QTransform glyph_transform;
            glyph_transform.scale(resolved_scale_x, resolved_scale_y);
            glyph_path = glyph_transform.map(glyph_path);
            if (coverage_scale < 0.9999f) {
                QTransform coverage_transform;
                coverage_transform.scale(coverage_scale, coverage_scale);
                glyph_path = coverage_transform.map(glyph_path);
            }
        }
        if (glyph_path.isEmpty()) {
            const QRectF bounds = raw.boundingRect(glyph_id);
            if (!bounds.isEmpty() && bounds.height() > 0.0) {
                reason = "The shaped glyph face could not produce an outline path.";
                return false;
            }
            out = AtlasGlyph{};
            glyphs.emplace(key, out);
            return true;
        }

        const QRectF path_bounds = glyph_path.boundingRect();
        if (!path_bounds.isValid() || path_bounds.isEmpty()) {
            out = AtlasGlyph{};
            glyphs.emplace(key, out);
            return true;
        }

        const double left = std::floor(path_bounds.left());
        const double top = std::floor(path_bounds.top());
        const double right = std::ceil(path_bounds.right());
        const double bottom = std::ceil(path_bounds.bottom());
        const double requested_width = std::max(1.0, right - left);
        const double requested_height = std::max(1.0, bottom - top);
        if (!std::isfinite(requested_width) ||
            !std::isfinite(requested_height) ||
            requested_width > 1000000.0 ||
            requested_height > 1000000.0) {
            reason = "Glyph dimensions are outside the supported atlas range.";
            return false;
        }

        const int coverage_width = std::max(
            1, static_cast<int>(std::lround(requested_width)));
        const int coverage_height = std::max(
            1, static_cast<int>(std::lround(requested_height)));
        QImage outline(coverage_width, coverage_height,
                       QImage::Format_ARGB32_Premultiplied);
        outline.fill(Qt::transparent);
        {
            QPainter glyph_painter(&outline);
            glyph_painter.setRenderHint(QPainter::Antialiasing, true);
            glyph_painter.setRenderHint(QPainter::TextAntialiasing, true);
            glyph_painter.translate(-left, -top);
            glyph_painter.setPen(Qt::NoPen);
            glyph_painter.setBrush(Qt::white);
            glyph_painter.drawPath(glyph_path);
        }
        QImage gray = glyph_coverage_grayscale8(outline);
        if (gray.isNull()) {
            reason = "Could not rasterize the glyph outline.";
            return false;
        }
        const int sdf_padding = (raster_spread + 2) * 2;
        if (gray.width() + sdf_padding > kAtlasSize ||
            gray.height() + sdf_padding > kAtlasSize) {
            reason = "Glyph dimensions exceed one persistent atlas page.";
            return false;
        }
        int sdf_width = 0;
        int sdf_height = 0;
        std::vector<uint8_t> sdf = build_glyph_sdf(
            gray.constBits(), gray.width(), gray.height(),
            gray.bytesPerLine(), raster_spread, sdf_width, sdf_height);
        if (sdf.empty()) {
            reason = "Could not build the glyph distance field.";
            return false;
        }
        AtlasGlyph allocated;
        allocated.atlas_pixels_per_logical = coverage_scale;
        allocated.padding_logical =
            static_cast<float>(raster_spread + 2) / coverage_scale;
        allocated.offset_x = static_cast<float>(left) / coverage_scale;
        allocated.offset_y = static_cast<float>(top) / coverage_scale;
        if (!allocate_rect(sdf_width, sdf_height, allocated)) {
            reason = "The persistent glyph atlas reached its session limit.";
            return false;
        }
        AtlasPage &page = pages[static_cast<size_t>(allocated.page)];
        for (int y = 0; y < sdf_height; ++y) {
            std::copy(sdf.begin() + static_cast<size_t>(y) * sdf_width,
                      sdf.begin() + static_cast<size_t>(y + 1) * sdf_width,
                      page.pixels.begin() +
                          static_cast<size_t>(allocated.y + y) * kAtlasSize +
                          allocated.x);
        }
        page.mark_dirty(allocated.x, allocated.y, sdf_width, sdf_height);
        glyphs.emplace(key, allocated);
        out = allocated;
        return true;
    }

    bool upload_pages()
    {
        for (AtlasPage &page : pages) {
            if (!page.dirty && page.texture)
                continue;
            if (!page.texture) {
                const uint8_t *planes[1] = {page.pixels.data()};
                page.texture = gs_texture_create(kAtlasSize, kAtlasSize,
                                                 GS_R8, 1, planes,
                                                 GS_DYNAMIC);
            } else {
                /* gs_texture_map() is a discard-style write for dynamic OBS
                 * textures. Copying only the dirty rectangle leaves every
                 * untouched texel undefined and produced the random glyph
                 * fragments seen while typing, particularly with OpenGL on
                 * Linux. Rewrite this smaller page completely so all prior
                 * glyphs remain valid. */
                uint8_t *mapped = nullptr;
                uint32_t linesize = 0;
                const bool map_succeeded =
                    gs_texture_map(page.texture, &mapped, &linesize);
                if (map_succeeded) {
                    if (mapped) {
                        for (int y = 0; y < kAtlasSize; ++y) {
                            std::memcpy(
                                mapped + static_cast<size_t>(y) * linesize,
                                page.pixels.data() +
                                    static_cast<size_t>(y) * kAtlasSize,
                                static_cast<size_t>(kAtlasSize));
                        }
                    }
                    /* A successful map must always be paired with unmap, even
                     * on an unusual backend that returns a null data pointer. */
                    gs_texture_unmap(page.texture);
                }
                if (!map_succeeded || !mapped) {
                    gs_texture_set_image(page.texture, page.pixels.data(),
                                         kAtlasSize, false);
                }
            }
            if (!page.texture) {
                last_error = "Could not upload a persistent glyph-atlas page.";
                return false;
            }
            page.clear_dirty();
        }
        if (!white_texture) {
            const uint8_t white = 255;
            const uint8_t *planes[1] = {&white};
            white_texture = gs_texture_create(1, 1, GS_R8, 1, planes, 0);
            if (!white_texture) {
                last_error = "Could not allocate the text-decoration texture.";
                return false;
            }
        }
        return true;
    }
};

Layer::Layer() : impl_(std::make_unique<Impl>()) {}
Layer::~Layer() = default;
Layer::Layer(Layer &&) noexcept = default;
Layer &Layer::operator=(Layer &&) noexcept = default;

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;

bool Renderer::prepare(Layer &layer, const ImmutableTextLayout &layout,
                       const std::vector<TextLayoutPaintRun> &paint_runs,
                       const PrepareOptions &options,
                       const TextAnimatorEvaluation *animation,
                       std::string *reason)
{
    if (!impl_->backend_available) {
        if (reason)
            *reason = impl_->last_error;
        return false;
    }
    if (!layout || !layout->valid || options.logical_width <= 0.0f ||
        options.logical_height <= 0.0f || options.text_width <= 0.0f ||
        options.text_height <= 0.0f) {
        if (reason)
            *reason = "Invalid immutable text layout or target geometry.";
        return false;
    }

    RichTextFill default_fill;
    default_fill.color = 0xFFFFFFFFu;
    TextLayoutPaintRun default_paint;
    default_paint.byte_start = 0;
    default_paint.byte_length = layout->text.size();
    default_paint.style.fill = default_fill;
    std::vector<TextLayoutPaintRun> fallback_runs;
    const std::vector<TextLayoutPaintRun> *resolved_runs_ptr = &paint_runs;
    if (paint_runs.empty()) {
        fallback_runs.push_back(default_paint);
        resolved_runs_ptr = &fallback_runs;
    }
    const std::vector<TextLayoutPaintRun> &resolved_runs = *resolved_runs_ptr;

    for (const TextLayoutPaintRun &run : resolved_runs) {
        if (run.style.stroke.enabled &&
            run.style.stroke.width > static_cast<float>(kSdfSpread - 3)) {
            if (reason)
                *reason = "Inline text stroke exceeds the SDF atlas spread.";
            return false;
        }
        /* Glyph SDF coverage remains scale-correct for every stored join mode.
         * The join choice affects only the contour presentation; it must not
         * force scaled text back to the QTextDocument font-size workaround. */
    }

    std::vector<Batch> behind_strokes;
    std::vector<Batch> fills;
    std::vector<Batch> front_strokes;
    auto batch_for = [&](std::vector<Batch> &phase, DrawPart draw_part,
                         int page, size_t paint_index, bool solid) -> Batch & {
        const int encoded_page = solid ? -1 : page;
        /* Merge only adjacent compatible quads inside the same global paint
         * phase. Grouping across phases would break Behind/Front semantics;
         * grouping non-adjacent glyphs would reorder combining marks and
         * overlapping scripts. */
        if (!phase.empty()) {
            Batch &last = phase.back();
            if (last.page == encoded_page &&
                last.paint_index == paint_index &&
                last.solid_geometry == solid &&
                last.draw_part == draw_part)
                return last;
        }
        Batch batch;
        batch.page = encoded_page;
        batch.paint_index = paint_index;
        batch.solid_geometry = solid;
        batch.draw_part = draw_part;
        batch.material.fill = resolved_runs[paint_index].style.fill;
        batch.material.stroke = resolved_runs[paint_index].style.stroke;
        batch.vertices.reserve(96);
        batch.domain = {0.0f, 0.0f,
                        std::max(1.0f, options.text_width),
                        std::max(1.0f, options.text_height)};
        phase.push_back(std::move(batch));
        return phase.back();
    };

    std::vector<std::vector<TextLayoutPaintSlice>> cluster_slices;
    cluster_slices.reserve(layout->clusters.size());
    for (const TextLayoutCluster &cluster : layout->clusters) {
        std::vector<TextLayoutPaintSlice> slices =
            text_layout_cluster_paint_slices(*layout, cluster, resolved_runs);
        if (slices.empty()) {
            const size_t paint_index = std::min(
                paint_run_index_at(resolved_runs, cluster.byte_start),
                resolved_runs.size() - 1);
            slices.push_back(
                {paint_index, cluster.x, cluster.x + cluster.width});
        }
        cluster_slices.push_back(std::move(slices));
    }

    std::string failure_reason;
    for (const TextLayoutGlyph &glyph : layout->glyphs) {
        if (glyph.run_index >= layout->runs.size() ||
            glyph.cluster_index >= layout->clusters.size())
            continue;
        const TextLayoutRun &run = layout->runs[glyph.run_index];
        AtlasGlyph atlas_glyph;
        if (!impl_->glyph_for(run, glyph.glyph_id,
                              glyph.scale_x, glyph.scale_y,
                              options.raster_scale,
                              atlas_glyph, failure_reason)) {
            if (reason)
                *reason = failure_reason;
            return false;
        }
        if (atlas_glyph.page < 0 || atlas_glyph.width <= 0 ||
            atlas_glyph.height <= 0)
            continue;

        const float padding = atlas_glyph.padding_logical;

        /* v277: the atlas entry already contains the anisotropically transformed
         * outline. Emit it at 1:1 pixel geometry; applying scale again here would
         * double-transform the visible glyph while selection stays single-scaled. */
        Quad glyph_quad;
        glyph_quad.x0 = options.text_offset_x + glyph.x +
                        atlas_glyph.offset_x - padding;
        glyph_quad.y0 = options.text_offset_y + glyph.y +
                        atlas_glyph.offset_y - padding;
        glyph_quad.x1 = glyph_quad.x0 +
                        static_cast<float>(atlas_glyph.width) /
                            atlas_glyph.atlas_pixels_per_logical;
        glyph_quad.y1 = glyph_quad.y0 +
                        static_cast<float>(atlas_glyph.height) /
                            atlas_glyph.atlas_pixels_per_logical;
        glyph_quad.u0 = static_cast<float>(atlas_glyph.x) / kAtlasSize;
        glyph_quad.v0 = static_cast<float>(atlas_glyph.y) / kAtlasSize;
        glyph_quad.u1 = static_cast<float>(atlas_glyph.x + atlas_glyph.width) /
                        kAtlasSize;
        glyph_quad.v1 = static_cast<float>(atlas_glyph.y + atlas_glyph.height) /
                        kAtlasSize;
        glyph_quad.local_x0 = glyph_quad.x0 - options.text_offset_x;
        glyph_quad.local_y0 = glyph_quad.y0 - options.text_offset_y;
        glyph_quad.local_x1 = glyph_quad.x1 - options.text_offset_x;
        glyph_quad.local_y1 = glyph_quad.y1 - options.text_offset_y;
        glyph_quad.atlas_pixels_per_logical =
            atlas_glyph.atlas_pixels_per_logical;

        const std::vector<TextLayoutPaintSlice> &slices =
            cluster_slices[glyph.cluster_index];
        const TextAnimatorClusterState *cluster_animation =
            animation && glyph.cluster_index < animation->clusters.size()
                ? &animation->clusters[glyph.cluster_index] : nullptr;
        TextAnimatorClusterState adjusted_animation;
        if (cluster_animation && std::abs(cluster_animation->font_size_delta) > 1.0e-9) {
            adjusted_animation = *cluster_animation;
            const double base_size = std::max(1.0, static_cast<double>(run.font.pixel_size));
            const double factor = std::max(0.01,
                (base_size + adjusted_animation.font_size_delta) / base_size);
            adjusted_animation.scale_x *= factor;
            adjusted_animation.scale_y *= factor;
            cluster_animation = &adjusted_animation;
        }
        if (cluster_animation) {
            const double reveal_opacity =
                cluster_animation->reveal_direction == TextRevealDirection::None
                    ? cluster_animation->reveal : 1.0;
            if (cluster_animation->opacity * cluster_animation->visibility *
                    reveal_opacity <= 0.000001)
                continue;
        }
        const bool split_paint_cluster = slices.size() > 1;
        for (const TextLayoutPaintSlice &slice : slices) {
            const size_t paint_index =
                std::min(slice.paint_index, resolved_runs.size() - 1);
            const RichTextStroke &stroke = resolved_runs[paint_index].style.stroke;
            float outside_delta = 0.0f;
            float inside_delta = 0.0f;
            if (cluster_animation && stroke.enabled &&
                std::abs(cluster_animation->stroke_width_delta) > 1.0e-9) {
                const TextStrokeCoverageExtents base_extents =
                    text_stroke_coverage_extents(stroke.width, stroke.alignment);
                const TextStrokeCoverageExtents animated_extents =
                    text_stroke_coverage_extents(
                        std::max(0.0f, stroke.width + static_cast<float>(
                            cluster_animation->stroke_width_delta)),
                        stroke.alignment);
                outside_delta = animated_extents.outside - base_extents.outside;
                inside_delta = animated_extents.inside - base_extents.inside;
            }

            constexpr float kCoverageSamplingGuard = 4.0f;
            const float blur_padding = cluster_animation
                ? static_cast<float>(std::max(0.0, cluster_animation->blur))
                : 0.0f;
            const TextStrokeCoverageExtents stroke_extents =
                text_stroke_coverage_extents(
                    stroke.enabled ? std::max(0.0f, stroke.width) : 0.0f,
                    stroke.alignment);
            const float stroke_outside = std::max(
                0.0f, stroke_extents.outside + outside_delta);
            const float fill_padding = std::clamp(
                kCoverageSamplingGuard + blur_padding,
                kCoverageSamplingGuard, padding);
            const float stroke_padding = std::clamp(
                kCoverageSamplingGuard + blur_padding + stroke_outside,
                kCoverageSamplingGuard, padding);

            Quad fill_quad = glyph_quad;
            crop_quad_padding(fill_quad,
                std::max(0.0f, padding - fill_padding),
                std::max(0.0f, padding - fill_padding));
            bool fill_visible = true;
            if (run.split_ligature && run.clip_width > 0.0f &&
                run.clip_height > 0.0f) {
                fill_visible = clip_quad(fill_quad,
                    options.text_offset_x + run.clip_x,
                    options.text_offset_y + run.clip_y,
                    options.text_offset_x + run.clip_x + run.clip_width,
                    options.text_offset_y + run.clip_y + run.clip_height);
            }
            if (fill_visible && split_paint_cluster) {
                fill_visible = clip_quad(fill_quad,
                    options.text_offset_x + slice.x0,
                    -std::numeric_limits<float>::max(),
                    options.text_offset_x + slice.x1,
                    std::numeric_limits<float>::max());
            }
            if (fill_visible) {
                Batch &fill_batch = batch_for(
                    fills, DrawPart::Fill, atlas_glyph.page, paint_index, false);
                append_quad(fill_batch.vertices, fill_quad, options,
                            cluster_animation,
                            cluster_animation
                                ? static_cast<float>(cluster_animation->fill_opacity)
                                : 1.0f, false);
            }

            if (!stroke.enabled || stroke.width <= 0.0001f)
                continue;
            Quad stroke_quad = glyph_quad;
            crop_quad_padding(stroke_quad,
                std::max(0.0f, padding - stroke_padding),
                std::max(0.0f, padding - stroke_padding));
            bool stroke_visible = true;
            const float clip_guard = stroke_outside + kCoverageSamplingGuard;
            if (run.split_ligature && run.clip_width > 0.0f &&
                run.clip_height > 0.0f) {
                stroke_visible = clip_quad(stroke_quad,
                    options.text_offset_x + run.clip_x - clip_guard,
                    options.text_offset_y + run.clip_y - clip_guard,
                    options.text_offset_x + run.clip_x + run.clip_width + clip_guard,
                    options.text_offset_y + run.clip_y + run.clip_height + clip_guard);
            }
            if (stroke_visible && split_paint_cluster) {
                stroke_visible = clip_quad(stroke_quad,
                    options.text_offset_x + slice.x0 - clip_guard,
                    -std::numeric_limits<float>::max(),
                    options.text_offset_x + slice.x1 + clip_guard,
                    std::numeric_limits<float>::max());
            }
            if (!stroke_visible)
                continue;

            const int stroke_phase_index = text_stroke_draw_phase(stroke.on_front);
            std::vector<Batch> &stroke_phase = stroke_phase_index == 2
                ? front_strokes : behind_strokes;
            Batch &stroke_batch = batch_for(
                stroke_phase,
                stroke_phase_index == 2 ? DrawPart::FrontStroke
                                        : DrawPart::BehindStroke,
                atlas_glyph.page, paint_index, false);
            append_quad(stroke_batch.vertices, stroke_quad, options,
                        cluster_animation,
                        cluster_animation
                            ? static_cast<float>(cluster_animation->stroke_opacity)
                            : 1.0f,
                        true, outside_delta, inside_delta);
        }
    }

    /* Decorations are paint properties, not shaping properties. Build them
     * from the same cluster paint slices used by glyphs so a mixed-style run
     * can underline/strike only the selected byte range. The previous run-wide
     * implementation sampled the style at run start and therefore ignored
     * later underline/strikethrough changes inside a shaped run. */
    for (size_t cluster_index = 0;
         cluster_index < layout->clusters.size(); ++cluster_index) {
        const TextLayoutCluster &cluster = layout->clusters[cluster_index];
        if (cluster.line_index >= layout->lines.size() ||
            cluster.run_index >= layout->runs.size())
            continue;
        const TextLayoutLine &line = layout->lines[cluster.line_index];
        const TextLayoutRun &run = layout->runs[cluster.run_index];
        const float pixel_size = std::max(1.0f, run.font.pixel_size);
        const float thickness = std::max(1.0f, pixel_size * 0.055f);
        for (const TextLayoutPaintSlice &slice : cluster_slices[cluster_index]) {
            const size_t paint_index =
                std::min(slice.paint_index, resolved_runs.size() - 1);
            const TextLayoutPaintStyle &style =
                resolved_runs[paint_index].style;
            if (!style.underline && !style.strikethrough)
                continue;
            const float left = std::min(slice.x0, slice.x1);
            const float right = std::max(slice.x0, slice.x1);
            if (!(right > left))
                continue;
            auto add_decoration = [&](float y) {
                Batch &batch = batch_for(fills, DrawPart::Fill, -1,
                                         paint_index, true);
                const float x0 = options.text_offset_x + left;
                const float x1 = options.text_offset_x + right;
                const float y0 = options.text_offset_y + y;
                const float y1 = y0 + thickness;
                append_quad(batch.vertices, x0, y0, x1, y1,
                            0.0f, 0.0f, 1.0f, 1.0f,
                            left, y, right, y + thickness, options);
            };
            if (style.underline)
                add_decoration(line.baseline + pixel_size * 0.08f);
            if (style.strikethrough)
                add_decoration(line.baseline - pixel_size * 0.32f);
        }
    }

    std::vector<Batch> batches;
    batches.reserve(behind_strokes.size() + fills.size() +
                    front_strokes.size());
    batches.insert(batches.end(),
                   std::make_move_iterator(behind_strokes.begin()),
                   std::make_move_iterator(behind_strokes.end()));
    batches.insert(batches.end(), std::make_move_iterator(fills.begin()),
                   std::make_move_iterator(fills.end()));
    batches.insert(batches.end(),
                   std::make_move_iterator(front_strokes.begin()),
                   std::make_move_iterator(front_strokes.end()));

    layer.impl_->options = options;
    layer.impl_->batches = std::move(batches);
    layer.impl_->pending = true;
    if (reason)
        reason->clear();
    return true;
}


bool Renderer::effect_ready() const
{
    std::lock_guard<std::mutex> effect_lock(impl_->effect_mutex);
    return impl_->backend_available && impl_->effect != nullptr;
}

bool Renderer::compile_effect()
{
    std::lock_guard<std::mutex> effect_lock(impl_->effect_mutex);
    if (impl_->effect)
        return true;
    if (!impl_->backend_available)
        return false;
    impl_->effect = gs_effect_create(kGpuTextEffect,
                                     "obs-fxm-gpu-text.effect", nullptr);
    if (!impl_->effect || !gs_effect_get_technique(impl_->effect, "Draw")) {
        if (impl_->effect) {
            gs_effect_destroy(impl_->effect);
            impl_->effect = nullptr;
        }
        impl_->backend_available = false;
        impl_->last_error =
            "Could not compile the Phase 12C GPU text shader.";
        return false;
    }
    return true;
}

bool Renderer::render(Layer &layer)
{
    std::lock_guard<std::mutex> effect_lock(impl_->effect_mutex);
    Layer::Impl &state = *layer.impl_;
    if (!state.pending)
        return texture(layer) != nullptr;
    if (!impl_->backend_available)
        return false;
    if (!impl_->effect) {
        impl_->last_error =
            "GPU text shader is not compiled yet.";
        return false;
    }
    if (!impl_->upload_pages())
        return false;

    const float scale = std::clamp(state.options.raster_scale, 0.01f, 8.0f);
    const uint32_t width = static_cast<uint32_t>(std::clamp(
        static_cast<int>(std::ceil(state.options.logical_width * scale)),
        1, 16384));
    const uint32_t height = static_cast<uint32_t>(std::clamp(
        static_cast<int>(std::ceil(state.options.logical_height * scale)),
        1, 16384));
    const int render_index = state.active_target == 0 ? 1 : 0;
    if (!state.targets[render_index])
        state.targets[render_index] = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    gs_texrender_t *target = state.targets[render_index];
    if (!target) {
        impl_->last_error = "Could not allocate a GPU text layer target.";
        return false;
    }

    gs_texrender_reset(target);
    if (!gs_texrender_begin(target, width, height)) {
        impl_->last_error = "Could not begin the GPU text layer target.";
        return false;
    }
    /* Glyph targets share the graphics context with hardware 3D passes.
     * Restore canonical state before clearing the alternate retained target;
     * otherwise an older upright glyph buffer can survive a Y move. */
    gs_enable_color(true, true, true, true);
    gs_enable_depth_test(false);
    gs_depth_function(GS_LEQUAL);
    gs_set_cull_mode(GS_NEITHER);
    gs_enable_blending(false);
    gs_ortho(0.0f, static_cast<float>(width), 0.0f,
             static_cast<float>(height), -100.0f, 100.0f);
    vec4 clear;
    vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR, &clear, 1.0f, 0);
    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    bool success = true;
    for (const Batch &batch : state.batches) {
        if (batch.vertices.empty())
            continue;
        gs_vertbuffer_t *vertex_buffer = create_vertex_buffer(batch.vertices);
        if (!vertex_buffer) {
            impl_->last_error = "Could not allocate a GPU glyph-quad buffer.";
            success = false;
            break;
        }
        gs_texture_t *atlas = batch.solid_geometry
            ? impl_->white_texture
            : impl_->pages[static_cast<size_t>(batch.page)].texture;
        if (gs_eparam_t *param =
                gpu_text_effect_param(impl_->effect, "glyphAtlas"))
            gs_effect_set_texture(param, atlas);
        set_int(impl_->effect, "coverageMode",
                batch.solid_geometry ? 1 : 0);
        set_int(impl_->effect, "drawPart",
                batch.draw_part == DrawPart::Fill ? 0 : 1);
        set_float(impl_->effect, "sdfSpread",
                  static_cast<float>(kSdfSpread));
        set_vec2(impl_->effect, "atlasTexelSize",
                 1.0f / static_cast<float>(kAtlasSize),
                 1.0f / static_cast<float>(kAtlasSize));
        set_vec2(impl_->effect, "materialOrigin", batch.domain.x,
                 batch.domain.y);
        set_vec2(impl_->effect, "materialSize", batch.domain.width,
                 batch.domain.height);
        if (!upload_gradient_lut(impl_->fill_gradient_lut,
                                 batch.material.fill, 1.0f) ||
            !upload_gradient_lut(impl_->stroke_gradient_lut,
                                 batch.material.stroke.fill,
                                 batch.material.stroke.opacity)) {
            impl_->last_error = "Could not upload a rich-text gradient ramp.";
            gs_vertexbuffer_destroy(vertex_buffer);
            success = false;
            break;
        }
        if (gs_eparam_t *param =
                gpu_text_effect_param(impl_->effect, "fillGradientLut"))
            gs_effect_set_texture(param, impl_->fill_gradient_lut);
        if (gs_eparam_t *param =
                gpu_text_effect_param(impl_->effect, "strokeGradientLut"))
            gs_effect_set_texture(param, impl_->stroke_gradient_lut);
        set_fill_params(impl_->effect, "fill", batch.material.fill, 1.0f);
        set_int(impl_->effect, "strokeEnabled",
                batch.draw_part == DrawPart::Fill
                    ? 0
                    : (batch.material.stroke.enabled ? 1 : 0));
        set_int(impl_->effect, "strokeAntialias",
                batch.material.stroke.antialias ? 1 : 0);
        const float stroke_width =
            std::max(0.0f, batch.material.stroke.width);
        const TextStrokeCoverageExtents stroke_extents =
            text_stroke_coverage_extents(
                stroke_width, batch.material.stroke.alignment);
        set_float(impl_->effect, "strokeWidth", stroke_width);
        set_float(impl_->effect, "strokeOutside",
                  stroke_extents.outside);
        set_float(impl_->effect, "strokeInside",
                  stroke_extents.inside);
        set_fill_params(impl_->effect, "stroke",
                        batch.material.stroke.fill,
                        batch.material.stroke.opacity);

        gs_load_vertexbuffer(vertex_buffer);
        while (gs_effect_loop(impl_->effect, "Draw"))
            gs_draw(GS_TRIS, 0,
                    static_cast<uint32_t>(batch.vertices.size()));
        gs_load_vertexbuffer(nullptr);
        gs_vertexbuffer_destroy(vertex_buffer);
    }
    gs_blend_state_pop();
    gs_texrender_end(target);
    if (!success)
        return false;
    gs_texture_t *rendered = gs_texrender_get_texture(target);
    if (!rendered) {
        impl_->last_error = "GPU text rendering produced no texture.";
        return false;
    }
    state.active_target = render_index;
    state.width = width;
    state.height = height;
    state.pending = false;
    state.batches.clear();
    impl_->last_error.clear();
    return true;
}

void Renderer::release_layer(Layer &layer)
{
    Layer::Impl &state = *layer.impl_;
    for (gs_texrender_t *&target : state.targets) {
        if (target)
            gs_texrender_destroy(target);
        target = nullptr;
    }
    state.active_target = -1;
    state.width = 0;
    state.height = 0;
    state.pending = false;
    state.batches.clear();
}

void Renderer::reset()
{
    std::lock_guard<std::mutex> effect_lock(impl_->effect_mutex);
    for (AtlasPage &page : impl_->pages) {
        if (page.texture)
            gs_texture_destroy(page.texture);
        page.texture = nullptr;
    }
    impl_->pages.clear();
    impl_->glyphs.clear();
    impl_->raw_fonts.clear();
    if (impl_->white_texture)
        gs_texture_destroy(impl_->white_texture);
    impl_->white_texture = nullptr;
    if (impl_->fill_gradient_lut)
        gs_texture_destroy(impl_->fill_gradient_lut);
    impl_->fill_gradient_lut = nullptr;
    if (impl_->stroke_gradient_lut)
        gs_texture_destroy(impl_->stroke_gradient_lut);
    impl_->stroke_gradient_lut = nullptr;
    if (impl_->effect)
        gs_effect_destroy(impl_->effect);
    impl_->effect = nullptr;
    impl_->backend_available = true;
    impl_->last_error.clear();
}

gs_texture_t *Renderer::texture(const Layer &layer) const
{
    const Layer::Impl &state = *layer.impl_;
    if (state.active_target < 0 || state.active_target > 1 ||
        !state.targets[state.active_target])
        return nullptr;
    return gs_texrender_get_texture(state.targets[state.active_target]);
}

uint32_t Renderer::texture_width(const Layer &layer) const
{
    return layer.impl_->width;
}

uint32_t Renderer::texture_height(const Layer &layer) const
{
    return layer.impl_->height;
}

bool Renderer::owns_texture(const Layer &layer,
                            const gs_texture_t *candidate) const
{
    if (!candidate)
        return false;
    for (gs_texrender_t *target : layer.impl_->targets) {
        if (target && gs_texrender_get_texture(target) == candidate)
            return true;
    }
    return false;
}

bool Renderer::backend_available() const
{
    return impl_->backend_available;
}

const char *Renderer::last_error() const
{
    return impl_->last_error.empty() ? nullptr : impl_->last_error.c_str();
}

} // namespace fxm::gpu_text
