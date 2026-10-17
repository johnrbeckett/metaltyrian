// Metal shaders, compiled at launch so the package needs no Metal build step.
// Display modes and HDR glow as tuned in MetalPreview (Research/metal-preview),
// with John's picks: sharp pixels or authentic VGA, glow strength 1.2.

enum ShaderSource {
    static let core = #"""
#include <metal_stdlib>
using namespace metal;

struct VOut {
    float4 pos [[position]];
    float2 uv;
};

// One triangle that covers the whole target. uv has its origin at the top left.
vertex VOut fullscreen_vert(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));
    VOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}

struct DisplayParams {
    float2 texSize;   // size of the game picture, 320x200
    float2 outSize;   // size of the viewport in pixels
    float mode;       // 0 sharp pixels, 1 authentic VGA
    float hdr;        // 1 = HDR glow on
    float glow;       // glow strength
    float headroom;   // current EDR headroom, 1.0 on an SDR screen
    float vgaLines;   // 400: VGA drew 320x200 double-scanned
    float pad;
};

static float hotness(float3 lin) {
    float peak = max(lin.r, max(lin.g, lin.b));
    float sat = peak - min(lin.r, min(lin.g, lin.b));
    return saturate((peak - 0.55) * 3.0) * saturate((sat - 0.45) * 3.0);
}

fragment float4 display_frag(VOut in [[stage_in]],
                             texture2d<float> img [[texture(0)]],
                             texture2d<float> glowN [[texture(1)]],
                             texture2d<float> glowW [[texture(2)]],
                             constant DisplayParams &p [[buffer(0)]]) {
    constexpr sampler lin(filter::linear, address::clamp_to_edge);

    // Sharp pixels: nearest-neighbour inside each texel, a one-pixel blend at the edges.
    float2 texel = in.uv * p.texSize;
    float2 scale = max(floor(p.outSize / p.texSize), float2(1.0));
    float2 tf = floor(texel);
    float2 cd = fract(texel) - 0.5;
    float2 rr = 0.5 - 0.5 / scale;
    float2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;
    float3 c = img.sample(lin, (tf + f) / p.texSize).rgb;
    float3 linc = pow(c, float3(2.2));

    if (p.mode > 0.5) {
        // Authentic VGA: faint double-scanned lines, a fine aperture grille.
        float fy = fract(in.uv.y * p.vgaLines);
        float prof = 0.72 + 0.28 * exp(-((fy - 0.5) * (fy - 0.5)) / (2.0 * 0.30 * 0.30));
        float3 mask = float3(0.86);
        mask[uint(in.pos.x) % 3u] = 1.0;
        linc = linc * prof * mask * 1.10;
    }

    if (p.hdr > 0.5) {
        // Lasers, fire and engines: lift the pixel a little into EDR, add a soft bloom.
        float edr = min(max(p.headroom - 1.0, 0.0), 1.5);
        float boost = 1.0 + hotness(linc) * edr * 0.35 * p.glow;
        float3 g = glowN.sample(lin, in.uv).rgb * 0.55 + glowW.sample(lin, in.uv).rgb * 0.35;
        linc = linc * boost + g * p.glow;
    }
    return float4(linc, 1.0);
}

// Bright pass at source resolution: keep only hot, saturated pixels, in linear light.
fragment float4 bright_frag(VOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler pt(filter::nearest, address::clamp_to_edge);
    float3 linc = pow(src.sample(pt, in.uv).rgb, float3(2.2));
    return float4(linc * hotness(linc), 1.0);
}

struct BlurParams {
    float2 dir;
    float sigma;
    float radius;
};

fragment float4 blur_frag(VOut in [[stage_in]],
                          texture2d<float> src [[texture(0)]],
                          constant BlurParams &b [[buffer(0)]]) {
    constexpr sampler pt(filter::nearest, address::clamp_to_edge);
    float2 texel = 1.0 / float2(src.get_width(), src.get_height());
    float3 acc = float3(0.0);
    float wsum = 0.0;
    int r = int(b.radius);
    for (int i = -r; i <= r; ++i) {
        float fi = float(i);
        float w = exp(-(fi * fi) / (2.0 * b.sigma * b.sigma));
        acc += src.sample(pt, in.uv + b.dir * texel * fi).rgb * w;
        wsum += w;
    }
    return float4(acc / wsum, 1.0);
}
"""#
}
