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
    float textOn;     // 1 = composite the crisp text overlay
    float titleOn;    // 1 = the high-resolution title art replaces the picture
    float titleFade;  // the title picture's palette fade, 0-1
    float4 logo0;     // TYRIAN logo box, game pixels (z = 0: hidden)
    float4 logo1;     // year logo box
    float4 aspects;   // width/height of the background and the two logos
    float4 hole;      // game pixels where the game's own picture shows (z = 0: none)
    float4 lightRect; // the playfield, game pixels, where dynamic light falls (z = 0: none)
    float light;      // dynamic light gain, 0 = off
    float edges;      // edge smoothing strength, 0 = off
    float4 controlsRect;  // the controls overlay's box, game pixels
    float controlsAlpha;  // its opacity, 0 = hidden
};

// Edge smoothing. Every playfield pixel knows the layer it was drawn in
// (background 0, islands, enemies, shots, ship, explosions higher) and the
// colour it covered. Around each point the four nearest pixels' layers are
// blended (bilinear); where a layer's share crosses one half is its smooth
// outline. Stair-steps become clean diagonals, corners round off, and what
// shows past the outline is the real colour underneath. Inside a shape
// nothing changes: the pixel art stays crisp.
static float3 smooth_edges(float3 c, float2 gp, float4 hole,
                           texture2d<float> img, texture2d<float> layer,
                           texture2d<float> under, texture2d<float> floatMask,
                           float aaw) {
    int2 P = int2(floor(gp));
    if (floatMask.read(uint2(P)).r > 0.5) return c;   // the floating overlays stay as they are
    float2 q = gp - 0.5;
    int2 B = int2(floor(q));
    float2 f = q - float2(B);
    int2 cell[4] = { B, B + int2(1, 0), B + int2(0, 1), B + int2(1, 1) };
    float w[4] = { (1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y };
    int L[4];
    bool solid[4];   // outside the playfield or a floating overlay: never an outline
    int2 lo = int2(hole.xy), hi = int2(hole.xy + hole.zw) - 1;
    for (int i = 0; i < 4; i++) {
        int2 k = clamp(cell[i], int2(0), int2(319, 199));
        bool outside = any(cell[i] < lo) || any(cell[i] > hi);
        solid[i] = outside || floatMask.read(uint2(k)).r > 0.5;
        L[i] = int(round(layer.read(uint2(k)).r * 255.0));
    }
    int LP = int(round(layer.read(uint2(P)).r * 255.0));

    // This pixel's own shape: where its layer's share drops below one half,
    // what it covered shows through.
    float vOwn = 0.0;
    for (int i = 0; i < 4; i++)
        vOwn += (solid[i] || L[i] >= LP) ? w[i] : 0.0;
    float3 cp = img.read(uint2(P)).rgb;
    float3 r = LP > 0 ? mix(under.read(uint2(P)).rgb, cp, smoothstep(0.5 - aaw, 0.5 + aaw, vOwn)) : cp;

    // Shapes drawn above this pixel reach over it where their share passes
    // one half (lowest first, so the highest ends on top).
    int done = LP;
    for (int pass = 0; pass < 3; pass++) {
        int best = 1000, bi = -1;
        for (int i = 0; i < 4; i++)
            if (!solid[i] && L[i] > done && (L[i] < best || (L[i] == best && w[i] > w[bi]))) { best = L[i]; bi = i; }
        if (bi < 0) break;
        float v = 0.0;
        for (int j = 0; j < 4; j++)
            v += (!solid[j] && L[j] >= best) ? w[j] : 0.0;
        float a = smoothstep(0.5 - aaw, 0.5 + aaw, v);
        r = mix(r, img.read(uint2(clamp(cell[bi], int2(0), int2(319, 199)))).rgb, a);
        done = best;
    }
    return r;
}

static float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// A logo drawn into its box (game pixels), keeping the art's shape.
static float3 title_logo(float3 under, texture2d<float> tex, float4 box, float aspect,
                         float2 uv, float2 outSize, sampler s) {
    if (box.z <= 0.0) return under;
    float2 g = uv * float2(320.0, 200.0);
    float2 l = (g - box.xy) / box.zw;
    float boxAspect = (box.z * outSize.x / 320.0) / (box.w * outSize.y / 200.0);
    if (boxAspect > aspect) l.x = 0.5 + (l.x - 0.5) * (boxAspect / aspect);
    else l.y = 0.5 + (l.y - 0.5) * (aspect / boxAspect);
    if (any(l < 0.0) || any(l > 1.0)) return under;
    float4 c = tex.sample(s, l);
    return mix(under, c.rgb, c.a);
}

static float hotness(float3 lin) {
    float peak = max(lin.r, max(lin.g, lin.b));
    float sat = peak - min(lin.r, min(lin.g, lin.b));
    return saturate((peak - 0.55) * 3.0) * saturate((sat - 0.45) * 3.0);
}

fragment float4 display_frag(VOut in [[stage_in]],
                             texture2d<float> img [[texture(0)]],
                             texture2d<float> glowN [[texture(1)]],
                             texture2d<float> glowW [[texture(2)]],
                             texture2d<float> overlay [[texture(3)]],
                             texture2d<float> titleBg [[texture(4)]],
                             texture2d<float> titleLogo0 [[texture(5)]],
                             texture2d<float> titleLogo1 [[texture(6)]],
                             texture2d<float> lightMap [[texture(7)]],
                             texture2d<float> floatMask [[texture(8)]],
                             texture2d<float> layerMap [[texture(9)]],
                             texture2d<float> underMap [[texture(10)]],
                             texture2d<float> controlsTex [[texture(11)]],
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

    // High-resolution art (the title, or a picture's regenerated art), except
    // in the hole the game keeps for itself (the playfield inside the HUD).
    float2 gp = in.uv * float2(320.0, 200.0);
    float inHole = (p.hole.z > 0.0 && all(gp >= p.hole.xy) && all(gp < p.hole.xy + p.hole.zw)) ? 1.0 : 0.0;
    float art = p.titleOn * (1.0 - inHole);
    if (art > 0.5) {
        // The art fills the picture (trimmed to fit); the title's logos sit
        // where the game puts its own; all fade with the screen.
        constexpr sampler ms(filter::linear, mip_filter::linear, address::clamp_to_edge);
        float a = p.outSize.x / p.outSize.y, ia = p.aspects.x;
        float2 buv = in.uv;
        if (a > ia) buv.y = 0.5 + (in.uv.y - 0.5) * (ia / a);
        else buv.x = 0.5 + (in.uv.x - 0.5) * (a / ia);
        float3 t = titleBg.sample(ms, buv).rgb;
        t = title_logo(t, titleLogo0, p.logo0, p.aspects.y, in.uv, p.outSize, ms);
        t = title_logo(t, titleLogo1, p.logo1, p.aspects.z, in.uv, p.outSize, ms);
        c = t * p.titleFade;
    }

    if (p.edges > 0.0 && p.hole.z > 0.0 && inHole > 0.5) {
        // About one screen pixel of anti-aliasing along each outline.
        float aaw = clamp(0.5 * p.texSize.x / p.outSize.x, 0.02, 0.5);
        float3 sm = smooth_edges(c, gp, p.hole, img, layerMap, underMap, floatMask, aaw);
        c = mix(c, sm, p.edges);
    }

    if (p.hole.z > 0.0 && inHole > 0.5) {
        // The playfield's own overlays (score, lives, special, bombs) float:
        // a soft shadow under each, 2 game pixels down and right, on the
        // playfield only (not on the overlays themselves), under the crisp text.
        constexpr sampler mpt(filter::nearest, address::clamp_to_edge);
        float2 t = 1.0 / p.texSize;
        float2 sp = in.uv - 2.0 * t;
        float acc = 0.0, wsum = 0.0;
        for (int j = -2; j <= 2; j++) {
            for (int i = -2; i <= 2; i++) {
                float w = exp(-float(i * i + j * j) / 3.0);
                acc += floatMask.sample(lin, sp + float2(i, j) * t * 0.8).r * w;
                wsum += w;
            }
        }
        float self = floatMask.sample(mpt, in.uv).r;
        c *= 1.0 - 0.65 * (acc / wsum) * (1.0 - self);
    }

    if (p.textOn > 0.5) {
        // Crisp text, on a grid 3x finer than the game's pixels, sampled the same sharp way.
        float2 ts = p.texSize * 3.0;
        float2 ot = in.uv * ts;
        float2 os = max(floor(p.outSize / ts), float2(1.0));
        float2 od = fract(ot) - 0.5;
        float2 orr = 0.5 - 0.5 / os;
        float2 of = (od - clamp(od, -orr, orr)) * os + 0.5;
        float4 o = overlay.sample(lin, (floor(ot) + of) / ts);   // premultiplied
        c = c * (1.0 - o.a) + o.rgb;
    }
    float3 linc = pow(c, float3(2.2));

    if (p.light > 0.0 && p.lightRect.z > 0.0 && all(gp >= p.lightRect.xy) &&
        all(gp < p.lightRect.xy + p.lightRect.zw)) {
        // Dynamic light: shots, engines and explosions light up what is near
        // them, in their colour. The surface's shape is guessed from the
        // art's own shading (brighter = raised), so edges facing the light
        // catch more of it.
        constexpr sampler pt(filter::nearest, address::clamp_to_edge);
        float2 t = 1.0 / p.texSize;
        float2 cuv = (tf + 0.5) / p.texSize;     // this game pixel
        float3 L = min(lightMap.sample(lin, cuv).rgb * p.light, float3(1.5));
        float hl = luma(img.sample(pt, cuv - float2(t.x, 0)).rgb), hr = luma(img.sample(pt, cuv + float2(t.x, 0)).rgb);
        float hu = luma(img.sample(pt, cuv - float2(0, t.y)).rgb), hd = luma(img.sample(pt, cuv + float2(0, t.y)).rgb);
        float3 n = normalize(float3((hl - hr) * 2.0, (hu - hd) * 2.0, 1.0));
        float ll = luma(lightMap.sample(lin, cuv - float2(t.x, 0)).rgb), lr = luma(lightMap.sample(lin, cuv + float2(t.x, 0)).rgb);
        float lu = luma(lightMap.sample(lin, cuv - float2(0, t.y)).rgb), ld = luma(lightMap.sample(lin, cuv + float2(0, t.y)).rgb);
        float3 dir = normalize(float3(lr - ll, ld - lu, 0.02 / max(p.light, 1.0)) + 1e-6);
        float shape = 0.5 + 0.9 * saturate(dot(n, dir));
        linc += (linc * 0.5 + luma(linc) * 0.5 + 0.06) * L * shape;
    }

    if (p.mode > 0.5) {
        // Authentic VGA: faint double-scanned lines, a fine aperture grille.
        float fy = fract(in.uv.y * p.vgaLines);
        float prof = 0.72 + 0.28 * exp(-((fy - 0.5) * (fy - 0.5)) / (2.0 * 0.30 * 0.30));
        float3 mask = float3(0.86);
        mask[uint(in.pos.x) % 3u] = 1.0;
        // The scanlines and grille take about a sixth of the light; this gives
        // it back, plus the same small lift as Sharp.
        linc = linc * prof * mask * 1.34;
    } else {
        // A small lift (John: "a little dim"), before the glow, so the
        // effects keep their punch on top.
        linc *= 1.12;
    }

    if (p.hdr > 0.5) {
        // Lasers, fire and engines: lift the pixel a little into EDR, add a soft bloom.
        float edr = min(max(p.headroom - 1.0, 0.0), 1.5);
        float boost = 1.0 + hotness(linc) * edr * 0.35 * p.glow;
        // (The glow is made from the game's own picture, so not on the art.)
        float3 g = (glowN.sample(lin, in.uv).rgb * 0.55 + glowW.sample(lin, in.uv).rgb * 0.35) * (1.0 - art);
        linc = linc * boost + g * p.glow;
    }
    // The playfield sits inset in the HUD: a soft shadow along its edges
    // under the HUD frame (right and bottom), as if the frame stood proud.
    if (p.hole.z > 0.0 && inHole > 0.5) {
        float dx = p.hole.x + p.hole.z - gp.x;
        float dy = p.hole.y + p.hole.w - gp.y;
        float sh = max(1.0 - smoothstep(0.0, 10.0, dx), 1.0 - smoothstep(0.0, 10.0, dy));
        linc *= 1.0 - 0.7 * pow(sh, 1.6);
    }
    // The controls overlay, on top of everything (premultiplied, sRGB-ish).
    if (p.controlsAlpha > 0.0) {
        float2 cuv = (gp - p.controlsRect.xy) / p.controlsRect.zw;
        if (all(cuv >= 0.0) && all(cuv <= 1.0)) {
            float4 o = controlsTex.sample(lin, cuv);
            float a = o.a * p.controlsAlpha;
            float3 oc = o.a > 0.0 ? pow(o.rgb / o.a, float3(2.2)) : float3(0.0);
            linc = linc * (1.0 - a) + oc * a;
        }
    }
    return float4(linc, 1.0);
}

struct AmbientParams {
    float2 origin;    // picture's top-left corner in the drawable, pixels
    float2 size;      // picture's size in the drawable, pixels
};

// Ambient borders: a heavily blurred copy of the picture, edge colours carried
// outwards and dimmed with distance, like light spilling from a screen.
fragment float4 ambient_frag(VOut in [[stage_in]],
                             texture2d<float> amb [[texture(0)]],
                             constant AmbientParams &a [[buffer(0)]]) {
    constexpr sampler lin(filter::linear, address::clamp_to_edge);
    float2 uv = (in.pos.xy - a.origin) / a.size;
    float3 c = pow(amb.sample(lin, uv).rgb, float3(2.2));
    // Distance outside the picture, in picture heights.
    float2 out2 = max(max(-uv, uv - 1.0), 0.0) * float2(a.size.x / a.size.y, 1.0);
    float d = length(out2);
    float fade = 0.38 * exp(-d * 2.2);
    return float4(c * fade, 1.0);
}

struct EmitParams {
    float4 rect;      // the playfield, game pixels: light comes only from inside it
};

// Light sources for dynamic light: the same hot pixels as the glow, inside the playfield.
fragment float4 emit_frag(VOut in [[stage_in]], texture2d<float> src [[texture(0)]],
                          constant EmitParams &e [[buffer(0)]]) {
    constexpr sampler pt(filter::nearest, address::clamp_to_edge);
    float2 g = in.uv * float2(320.0, 200.0);
    if (e.rect.z <= 0.0 || any(g < e.rect.xy) || any(g >= e.rect.xy + e.rect.zw)) return float4(0.0, 0.0, 0.0, 1.0);
    float3 linc = pow(src.sample(pt, in.uv).rgb, float3(2.2));
    return float4(linc * hotness(linc), 1.0);
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
