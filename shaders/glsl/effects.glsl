// 内置效果，与 shaders/effects.msl 逐行对应。所有效果只面对标准朝向的 RGBA8。

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
