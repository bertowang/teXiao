#!/usr/bin/env python3
"""把 shader 源码嵌进 C++ 头文件，提交生成物，保证 Xcode / CMake / Gradle 构建无需 Python。

  shaders/*.msl          -> src/platform/metal/msl_shaders.h       (Metal, iOS/macOS)
  shaders/glsl/*.glsl    -> src/platform/gles/gles_shaders.h       (GLES3, Android)

改动任一 shader 后重跑本脚本即可。
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
MSL_SOURCES = ["common.msl", "normalize.msl", "effects.msl"]
GLSL_PARTS = {
    "kVertexSource": "fullscreen.vert",
    "kCommonSource": "common.glsl",
    "kNormalizeSource": "normalize.glsl",
    "kEffectsSource": "effects.glsl",
}

DELIM = "FXGLSL"
MSL_DELIM = "FXMSL"


def read_all(directory, names):
    return "\n".join(
        f"// ---- {name} ----\n{(ROOT / directory / name).read_text(encoding='utf-8')}"
        for name in names
    )


def cxx_string(text, delimiter):
    if f'R"{delimiter}(' in text or f'){delimiter}"' in text:
        raise SystemExit(f"shader source contains the raw-string delimiter: {delimiter}")
    return f'R"{delimiter}(' + text + f'){delimiter}"'


def main():
    # ---- Metal ----
    msl = read_all("shaders", MSL_SOURCES)
    out = ROOT / "src/platform/metal/msl_shaders.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        "#pragma once\n"
        "// 由 tools/embed_shaders.py 生成，请勿手改。\n"
        "namespace fx { namespace metal {\n"
        "inline constexpr const char* kMSLSource = " + cxx_string(msl, MSL_DELIM) + ";\n"
        "}  // namespace metal\n}  // namespace fx\n",
        encoding="utf-8")
    print(f"generated {out.relative_to(ROOT)} ({len(msl)} chars)")

    # ---- OpenGL ES ----
    lines = [
        "#pragma once",
        "// 由 tools/embed_shaders.py 生成，请勿手改。",
        "// GLES 后端把 kCommonSource + kNormalizeSource + kEffectsSource + main() 拼成",
        "// 一个 fragment shader；各 fragmentFunction 是同名的 vec4 fn(vec2 uv)。",
        "namespace fx { namespace gles {",
    ]
    for symbol, filename in GLSL_PARTS.items():
        text = (ROOT / "shaders/glsl" / filename).read_text(encoding="utf-8")
        lines.append(f"inline constexpr const char* {symbol} = {cxx_string(text, DELIM)};")
    lines += ["}  // namespace gles", "}  // namespace fx", ""]

    out = ROOT / "src/platform/gles/gles_shaders.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"generated {out.relative_to(ROOT)} ({len(GLSL_PARTS)} sources)")


if __name__ == "__main__":
    main()
