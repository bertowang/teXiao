#version 300 es
// 与 common.msl 的 vsFullscreen 完全一致：全屏三角形、无 VBO，uv 保持 Metal 的
// 左上原点约定 (0,1)/(2,1)/(0,-1)；采样时的 Y 翻转统一在 common.glsl 的 T0..T3 做。
const vec2 kPos[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
const vec2 kUv[3] = vec2[3](vec2(0.0, 1.0), vec2(2.0, 1.0), vec2(0.0, -1.0));

out vec2 vUv;

void main() {
    gl_Position = vec4(kPos[gl_VertexID], 0.0, 1.0);
    vUv = kUv[gl_VertexID];
}
