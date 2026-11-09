#version 300 es
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
