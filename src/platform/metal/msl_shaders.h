#pragma once
// 由 tools/embed_shaders.py 生成，请勿手改。
namespace fx { namespace metal {
inline constexpr const char* kMSLSource = R"FXMSL(// ---- common.msl ----
// shaders/common.msl —— 全后端共享约定的 Metal 实现；后续可按目录补 glsl / hlsl / spirv。
// 由 tools/embed_shaders.py 拼成一段源码，运行时用 newLibraryWithSource 编译，
// 因此 Xcode 工程不需要 .metal 构建规则，未来替换为 bgfx .sc 也不影响内核。
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4 p0;              // 通用尺寸 / 纹素 / 上屏宽高
    float4 p1;              // 效果参数槽 0
    float4 p2;              // 效果参数槽 1
    float4 p3;              // 时间 / alpha / videoRange / flipY
    float4 transform[3];    // 输出 uv -> 输入 uv 的 3x3 矩阵（按行）
    float4 anchor;          // 锚点 x, y, 宽高比, 保留
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
};

// 全屏三角形，无顶点缓冲。所有 Pass 共用同一套 uv 约定，故无需额外 Y 翻转。
vertex VSOut vsFullscreen(uint vid [[vertex_id]]) {
    const float2 pos[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    const float2 uvs[3] = { float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0) };
    VSOut out;
    out.position = float4(pos[vid], 0.0, 1.0);
    out.uv = uvs[vid];
    return out;
}

inline float2 transformedUV(float2 uv, constant Uniforms& U) {
    float3 p = float3(uv, 1.0);
    return float2(dot(p, U.transform[0].xyz), dot(p, U.transform[1].xyz));
}

// 64³ LUT 打成 8x8 的 64x64 小图，共 512x512。采样点夹在格内，避免跨格渗色。
inline float2 lutUV(float2 rg, float blueIndex) {
    float tileX = fmod(blueIndex, 8.0);
    float tileY = floor(blueIndex / 8.0);
    float2 inTile = clamp(rg, 0.5 / 64.0, 63.5 / 64.0);
    return float2((tileX * 64.0 + inTile.x * 64.0) / 512.0,
                  (tileY * 64.0 + inTile.y * 64.0) / 512.0);
}

// ---- normalize.msl ----
// shaders/normalize.msl —— Graph 第 0 节点：外部纹理 -> 标准朝向 RGBA8。
// 拼接在 common.msl 之后，共用 Uniforms / VSOut / transformedUV。

fragment float4 fsNormalizeNV12(VSOut in [[stage_in]],
                                constant Uniforms& U [[buffer(0)]],
                                texture2d<float> yTex  [[texture(0)]],
                                texture2d<float> uvTex [[texture(1)]],
                                sampler smp [[sampler(0)]]) {
    float2 uv = transformedUV(in.uv, U);
    float y = yTex.sample(smp, uv).r;
    float2 cbcr = uvTex.sample(smp, uv).rg - float2(0.5);
    float yv = y;
    if (U.p3.z > 0.5) {                       // video range 16-235
        yv = (y - 16.0 / 255.0) * (255.0 / 219.0);
        cbcr *= 255.0 / 224.0;
    }
    float3 rgb = float3(yv + 1.402 * cbcr.y,
                        yv - 0.344136 * cbcr.x - 0.714136 * cbcr.y,
                        yv + 1.772 * cbcr.x);  // BT.601
    return float4(clamp(rgb, 0.0, 1.0), 1.0);
}

fragment float4 fsNormalizeBGRA(VSOut in [[stage_in]],
                                constant Uniforms& U [[buffer(0)]],
                                texture2d<float> tex [[texture(0)]],
                                sampler smp [[sampler(0)]]) {
    return tex.sample(smp, transformedUV(in.uv, U)).bgra;
}

fragment float4 fsNormalizeRGBA(VSOut in [[stage_in]],
                                constant Uniforms& U [[buffer(0)]],
                                texture2d<float> tex [[texture(0)]],
                                sampler smp [[sampler(0)]]) {
    return tex.sample(smp, transformedUV(in.uv, U));
}

// ---- effects.msl ----
// shaders/effects.msl —— 内置效果（四要素中的 Shader 一项）。
// 所有效果只面对标准朝向的 RGBA8，参数全部来自参数表，不持有跨帧状态。

fragment float4 fsCopy(VSOut in [[stage_in]],
                       constant Uniforms& U [[buffer(0)]],
                       texture2d<float> src [[texture(0)]],
                       sampler smp [[sampler(0)]]) {
    return src.sample(smp, in.uv);
}

fragment float4 fsLUT(VSOut in [[stage_in]],
                      constant Uniforms& U [[buffer(0)]],
                      texture2d<float> src [[texture(0)]],
                      texture2d<float> lut [[texture(1)]],
                      sampler smp [[sampler(0)]]) {
    float4 c = src.sample(smp, in.uv);
    float blue = clamp(c.b, 0.0, 0.999999) * 63.0;
    float b0 = floor(blue);
    float b1 = min(b0 + 1.0, 63.0);
    float3 graded = mix(lut.sample(smp, lutUV(c.rg, b0)).rgb,
                        lut.sample(smp, lutUV(c.rg, b1)).rgb, blue - b0);
    return float4(mix(c.rgb, graded, clamp(U.p1.x, 0.0, 1.0)), c.a);
}

fragment float4 fsAdjust(VSOut in [[stage_in]],
                         constant Uniforms& U [[buffer(0)]],
                         texture2d<float> src [[texture(0)]],
                         sampler smp [[sampler(0)]]) {
    float4 c = src.sample(smp, in.uv);
    float3 rgb = (c.rgb - 0.5) * (1.0 + U.p1.y) + 0.5;
    rgb *= 1.0 + U.p1.x;
    float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    rgb = mix(float3(luma), rgb, 1.0 + U.p1.z);
    return float4(clamp(rgb, 0.0, 1.0), c.a);
}

// 值域加权的 5x5 双边近似：磨皮 / 柔肤（单 Pass 演示形态）。
fragment float4 fsBeauty(VSOut in [[stage_in]],
                         constant Uniforms& U [[buffer(0)]],
                         texture2d<float> src [[texture(0)]],
                         sampler smp [[sampler(0)]]) {
    float2 texel = float2(U.p0.z, U.p0.w) * max(U.p1.y, 0.5);
    float4 center = src.sample(smp, in.uv);
    float3 acc = center.rgb;
    float wsum = 1.0;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            if (i == 0 && j == 0) continue;
            float3 s = src.sample(smp, in.uv + float2(i, j) * texel).rgb;
            float d = distance(s, center.rgb);
            float w = exp(-d * d * 8.0) * exp(-float(i * i + j * j) / 8.0);
            acc += s * w;
            wsum += w;
        }
    }
    float3 smooth = acc / wsum;
    return float4(mix(center.rgb, smooth, clamp(U.p1.x, 0.0, 1.0)), center.a);
}

// 可分离高斯的一趟，方向由参数给定；素材里串两趟即横竖模糊。
fragment float4 fsBlur(VSOut in [[stage_in]],
                       constant Uniforms& U [[buffer(0)]],
                       texture2d<float> src [[texture(0)]],
                       sampler smp [[sampler(0)]]) {
    float2 dir = float2(U.p1.y, U.p1.z);
    if (length(dir) < 0.001) dir = float2(1.0, 0.0);
    float2 stepUV = normalize(dir) * float2(U.p0.z, U.p0.w) * max(U.p1.x, 0.25);
    float3 acc = src.sample(smp, in.uv).rgb * 0.227027;
    acc += src.sample(smp, in.uv + stepUV * 1.0).rgb * 0.1945946;
    acc += src.sample(smp, in.uv - stepUV * 1.0).rgb * 0.1945946;
    acc += src.sample(smp, in.uv + stepUV * 2.0).rgb * 0.1216216;
    acc += src.sample(smp, in.uv - stepUV * 2.0).rgb * 0.1216216;
    acc += src.sample(smp, in.uv + stepUV * 3.0).rgb * 0.054054;
    acc += src.sample(smp, in.uv - stepUV * 3.0).rgb * 0.054054;
    acc += src.sample(smp, in.uv + stepUV * 4.0).rgb * 0.016216;
    acc += src.sample(smp, in.uv - stepUV * 4.0).rgb * 0.016216;
    float4 center = src.sample(smp, in.uv);
    return float4(mix(center.rgb, acc, clamp(U.p1.w, 0.0, 1.0)), center.a);
}

// 贴纸 / 混合：静态资源按锚点绘制，可接 @ai.* 快照（M3）。
fragment float4 fsSticker(VSOut in [[stage_in]],
                          constant Uniforms& U [[buffer(0)]],
                          texture2d<float> src [[texture(0)]],
                          texture2d<float> sticker [[texture(1)]],
                          sampler smp [[sampler(0)]]) {
    float4 base = src.sample(smp, in.uv);
    float scale = max(U.p1.y, 0.01);
    float2 d = in.uv - float2(U.p1.z, U.p1.w);
    d.x *= max(U.anchor.z, 0.01);
    float2 st = d / scale + float2(0.5);
    float4 s = float4(0.0);
    if (st.x >= 0.0 && st.x <= 1.0 && st.y >= 0.0 && st.y <= 1.0) {
        s = sticker.sample(smp, st);
    }
    s.a *= clamp(U.p1.x, 0.0, 1.0);
    return float4(mix(base.rgb, s.rgb, s.a), 1.0);
}

// 上屏：处理分辨率与显示分辨率解耦，aspect-fill 裁剪 + 可选 Y 翻转。
fragment float4 fsPresent(VSOut in [[stage_in]],
                          constant Uniforms& U [[buffer(0)]],
                          texture2d<float> src [[texture(0)]],
                          sampler smp [[sampler(0)]]) {
    float inAspect = U.p0.x / max(U.p0.y, 1.0);
    float outAspect = U.p0.z / max(U.p0.w, 1.0);
    float2 uv = in.uv;
    if (inAspect > outAspect) {
        uv.x = (uv.x - 0.5) * (outAspect / inAspect) + 0.5;
    } else {
        uv.y = (uv.y - 0.5) * (inAspect / outAspect) + 0.5;
    }
    if (U.p3.w > 0.5) uv.y = 1.0 - uv.y;
    return src.sample(smp, uv);
}
)FXMSL";
}  // namespace metal
}  // namespace fx
