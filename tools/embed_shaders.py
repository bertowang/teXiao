#!/usr/bin/env python3
"""shaders/*.msl -> src/platform/metal/msl_shaders.h

提交生成物，保证 Xcode / CMake 构建无需 Python；改动 shader 后重跑本脚本即可。
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = ["common.msl", "normalize.msl", "effects.msl"]


def main():
    chunks = []
    for name in SOURCES:
        text = (ROOT / "shaders" / name).read_text(encoding="utf-8")
        chunks.append(f"// ---- {name} ----\n{text}")
    source = "\n".join(chunks)
    if "R\"FXMSL(" in source:
        raise SystemExit("MSL source contains the raw-string delimiter")

    out = ROOT / "src/platform/metal/msl_shaders.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        "#pragma once\n"
        "// 由 tools/embed_shaders.py 生成，请勿手改。\n"
        "namespace fx { namespace metal {\n"
        "inline constexpr const char* kMSLSource = R\"FXMSL(" + source + ")FXMSL\";\n"
        "}  // namespace metal\n}  // namespace fx\n",
        encoding="utf-8")
    print(f"generated {out.relative_to(ROOT)} ({len(source)} chars)")


if __name__ == "__main__":
    main()
