#!/usr/bin/env python3
"""features.json -> include/fx/generated/features.h (四级裁剪的第一级：生成)。"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    profile = sys.argv[1] if len(sys.argv) > 1 else None
    cfg = json.loads((ROOT / "features.json").read_text(encoding="utf-8"))
    profile = profile or cfg["defaultProfile"]
    if profile not in cfg["profiles"]:
        raise SystemExit(f"unknown profile: {profile}; valid={list(cfg['profiles'])}")
    enabled = set(cfg["profiles"][profile])

    lines = [
        "#pragma once",
        "// 由 tools/gen_features.py 从 features.json 生成，请勿手改。",
        f"// profile: {profile}",
        "// 每个宏都可由编译器 -D 覆盖；缺席能力必须提供 no-op 实现。",
        "#include \"fx/types.h\"",
    ]
    for name, meta in cfg["features"].items():
        value = 1 if name in enabled else 0
        lines += [f"#ifndef {name}", f"#define {name} {value}", "#endif"]
    lines += [
        "",
        "namespace fx {",
        "inline constexpr uint32_t kDefaultFeatureMask = 0",
    ]
    bit = {"FX_WITH_AI": "kFeatureAI", "FX_WITH_SCRIPT": "kFeatureScript",
           "FX_WITH_3D": "kFeature3D", "FX_WITH_EXPORT": "kFeatureExport"}
    for name in cfg["features"]:
        if name in enabled:
            lines.append(f"    | {bit[name]}")
    lines[-1] += ";"
    lines += ["}  // namespace fx", ""]

    out = ROOT / "include/fx/generated/features.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"generated {out.relative_to(ROOT)} (profile={profile})")


if __name__ == "__main__":
    main()
