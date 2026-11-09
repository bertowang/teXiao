#pragma once
// 由 tools/embed_shaders.py 生成，请勿手改。
// GLES 后端把 kCommonSource + kNormalizeSource + kEffectsSource + main() 拼成
// 一个 fragment shader；各 fragmentFunction 是同名的 vec4 fn(vec2 uv)。
namespace fx { namespace gles {
inline constexpr const char* kVertexSource = R"FXGLSL(#version 300 es
// 与 common.msl 的 vsFullscreen 完全一致：全屏三角形、无 VBO，uv 保持 Metal 的
// 左上原点约定 (0,1)/(2,1)/(0,-1)；采样时的 Y 翻转统一在 common.glsl 的 T0..T3 做。
const vec2 kPos[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
const vec2 kUv[3] = vec2[3](vec2(0.0, 1.0), vec2(2.0, 1.0), vec2(0.0, -1.0));

out vec2 vUv;

void main() {
    gl_Position = vec4(kPos[gl_VertexID], 0.0, 1.0);
    vUv = kUv[gl_VertexID];
}
)FXGLSL";
inline constexpr const char* kCommonSource = R"FXGLSL(#version 300 es
// 全后端共享约定的 GLSL ES 3.00 实现。uniform 与 C++ ShaderUniforms 128B 同构，
// 但用独立 uniform 上传（避免 std140 的 mat3 / 数组对齐陷阱）：4+3+1 = 8 个 vec4。
precision highp float;
precision highp int;

uniform vec4 U_p0;            // 效果: (w,h,1/w,1/h)；Present: (resultW,resultH,surfaceW,surfaceH)
uniform vec4 U_p1;            // 参数槽 0
uniform vec4 U_p2;            // 参数槽 1
uniform vec4 U_p3;            // 时间 / alpha / videoRange / flipY
uniform vec4 U_transform[3];  // 输出 uv -> 输入 uv 的 3x3 矩阵（按行，等价 MSL float4[3]）
uniform vec4 U_anchor;        // 锚点 x,y,宽高比,保留

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform sampler2D uTex2;
uniform sampler2D uTex3;

in vec2 vUv;
out vec4 fragColor;

// GL 纹理原点在左下，Metal 在左上。所有 CPU 上传纹理在 gles_device 里按行翻转成
// GL 的 upright 布局，这里把 Metal 空间的 uv 映射到 GL 空间；FBO 之间自洽。
#define T0(uv) texture(uTex0, vec2((uv).x, 1.0 - (uv).y))
#define T1(uv) texture(uTex1, vec2((uv).x, 1.0 - (uv).y))
#define T2(uv) texture(uTex2, vec2((uv).x, 1.0 - (uv).y))
#define T3(uv) texture(uTex3, vec2((uv).x, 1.0 - (uv).y))

vec2 transformedUV(vec2 uv) {
    vec3 p = vec3(uv, 1.0);
    return vec2(dot(p, U_transform[0].xyz), dot(p, U_transform[1].xyz));
}

// 64³ LUT 打成 8x8 的 64x64 小图，共 512x512。采样点夹在格内，避免跨格渗色。
vec2 lutUV(vec2 rg, float blueIndex) {
    float tileX = mod(blueIndex, 8.0);
    float tileY = floor(blueIndex / 8.0);
    vec2 inTile = clamp(rg, 0.5 / 64.0, 63.5 / 64.0);
    return vec2((tileX * 64.0 + inTile.x * 64.0) / 512.0,
                (tileY * 64.0 + inTile.y * 64.0) / 512.0);
}
)FXGLSL";
inline constexpr const char* kNormalizeSource = R"FXGLSL(// Graph 第 0 节点：外部纹理 -> 标准朝向 RGBA8。对应 shaders/normalize.msl。
// YUV_420_888 的两平面在 Java 侧整理成紧密的 R8(Y) / RG8(UV) 上传。

vec4 fsNormalizeNV12(vec2 uv) {
    vec2 st = transformedUV(uv);
    float y = T0(st).r;
    vec2 cbcr = T1(st).rg - vec2(0.5);
    float yv = y;
    if (U_p3.z > 0.5) {                       // video range 16-235
        yv = (y - 16.0 / 255.0) * (255.0 / 219.0);
        cbcr *= 255.0 / 224.0;
    }
    vec3 rgb = vec3(yv + 1.402 * cbcr.y,
                    yv - 0.344136 * cbcr.x - 0.714136 * cbcr.y,
                    yv + 1.772 * cbcr.x);    // BT.601
    return vec4(clamp(rgb, 0.0, 1.0), 1.0);
}

vec4 fsNormalizeBGRA(vec2 uv) {
    return T0(transformedUV(uv)).bgra;
}

vec4 fsNormalizeRGBA(vec2 uv) {
    return T0(transformedUV(uv));
}
)FXGLSL";
inline constexpr const char* kEffectsSource = R"FXGLSL(// 内置效果，与 shaders/effects.msl 逐行对应。所有效果只面对标准朝向的 RGBA8。

vec4 fsCopy(vec2 uv) {
    return T0(uv);
}

vec4 fsLUT(vec2 uv) {
    vec4 c = T0(uv);
    float blue = clamp(c.b, 0.0, 0.999999) * 63.0;
    float b0 = floor(blue);
    float b1 = min(b0 + 1.0, 63.0);
    vec3 graded = mix(T1(lutUV(c.rg, b0)).rgb,
                      T1(lutUV(c.rg, b1)).rgb, blue - b0);
    return vec4(mix(c.rgb, graded, clamp(U_p1.x, 0.0, 1.0)), c.a);
}

vec4 fsAdjust(vec2 uv) {
    vec4 c = T0(uv);
    vec3 rgb = (c.rgb - 0.5) * (1.0 + U_p1.y) + 0.5;
    rgb *= 1.0 + U_p1.x;
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(luma), rgb, 1.0 + U_p1.z);
    return vec4(clamp(rgb, 0.0, 1.0), c.a);
}

// 值域加权的 5x5 双边近似：磨皮 / 柔肤。
vec4 fsBeauty(vec2 uv) {
    vec2 texel = vec2(U_p0.z, U_p0.w) * max(U_p1.y, 0.5);
    vec4 center = T0(uv);
    vec3 acc = center.rgb;
    float wsum = 1.0;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            if (i == 0 && j == 0) continue;
            vec3 s = T0(uv + vec2(float(i), float(j)) * texel).rgb;
            float d = distance(s, center.rgb);
            float w = exp(-d * d * 8.0) * exp(-float(i * i + j * j) / 8.0);
            acc += s * w;
            wsum += w;
        }
    }
    vec3 smoothColor = acc / wsum;
    return vec4(mix(center.rgb, smoothColor, clamp(U_p1.x, 0.0, 1.0)), center.a);
}

// 可分离高斯的一趟，方向由参数给定；素材里串两趟即横竖模糊。
vec4 fsBlur(vec2 uv) {
    vec2 dir = vec2(U_p1.y, U_p1.z);
    if (length(dir) < 0.001) dir = vec2(1.0, 0.0);
    vec2 stepUV = normalize(dir) * vec2(U_p0.z, U_p0.w) * max(U_p1.x, 0.25);
    vec3 acc = T0(uv).rgb * 0.227027;
    acc += T0(uv + stepUV * 1.0).rgb * 0.1945946;
    acc += T0(uv - stepUV * 1.0).rgb * 0.1945946;
    acc += T0(uv + stepUV * 2.0).rgb * 0.1216216;
    acc += T0(uv - stepUV * 2.0).rgb * 0.1216216;
    acc += T0(uv + stepUV * 3.0).rgb * 0.054054;
    acc += T0(uv - stepUV * 3.0).rgb * 0.054054;
    acc += T0(uv + stepUV * 4.0).rgb * 0.016216;
    acc += T0(uv - stepUV * 4.0).rgb * 0.016216;
    vec4 center = T0(uv);
    return vec4(mix(center.rgb, acc, clamp(U_p1.w, 0.0, 1.0)), center.a);
}

// 贴纸 / 混合：静态资源按锚点绘制，可接 @ai.* 快照。
vec4 fsSticker(vec2 uv) {
    vec4 base = T0(uv);
    float scale = max(U_p1.y, 0.01);
    vec2 d = uv - vec2(U_p1.z, U_p1.w);
    d.x *= max(U_anchor.z, 0.01);
    vec2 st = d / scale + vec2(0.5);
    vec4 s = vec4(0.0);
    if (st.x >= 0.0 && st.x <= 1.0 && st.y >= 0.0 && st.y <= 1.0) {
        s = T1(st);
    }
    s.a *= clamp(U_p1.x, 0.0, 1.0);
    return vec4(mix(base.rgb, s.rgb, s.a), 1.0);
}

// 上屏：处理分辨率与显示分辨率解耦，aspect-fill 裁剪 + 可选 Y 翻转。
vec4 fsPresent(vec2 uv) {
    float inAspect = U_p0.x / max(U_p0.y, 1.0);
    float outAspect = U_p0.z / max(U_p0.w, 1.0);
    vec2 st = uv;
    if (inAspect > outAspect) {
        st.x = (st.x - 0.5) * (outAspect / inAspect) + 0.5;
    } else {
        st.y = (st.y - 0.5) * (inAspect / outAspect) + 0.5;
    }
    if (U_p3.w > 0.5) st.y = 1.0 - st.y;
    return T0(st);
}
)FXGLSL";
}  // namespace gles
}  // namespace fx
