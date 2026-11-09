// Graph 第 0 节点：外部纹理 -> 标准朝向 RGBA8。对应 shaders/normalize.msl。
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
