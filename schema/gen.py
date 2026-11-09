#!/usr/bin/env python3
"""schema/params.yaml -> include/fx/generated/params.h

构建期代码生成（与运行期 Graph 编译是两回事）：
产出 C++ 参数定义表、按名查找、素材 Schema 参数片段校验所需的元数据。
params.yaml 使用 YAML 1.2 的 JSON 兼容子集，允许以 # 开头的整行注释。
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_yaml_subset(path):
    text = "\n".join(line for line in path.read_text(encoding="utf-8").splitlines()
                     if not line.lstrip().startswith("#"))
    return json.loads(text)


def cxx_float(v):
    return f"{float(v):.6f}f"


def main():
    cfg = load_yaml_subset(ROOT / "schema/params.yaml")
    rows = []
    for p in cfg["passes"]:
        for param in p["params"]:
            rows.append(
                '    {{"{}", "{}", {}, {}, {}}},'.format(
                    p["shader"], param["name"], cxx_float(param["default"]),
                    cxx_float(param["min"]), cxx_float(param["max"])))

    lines = [
        "#pragma once",
        "// 由 schema/gen.py 从 schema/params.yaml 生成，请勿手改。",
        "#include <cstddef>",
        "#include <cstring>",
        "",
        "namespace fx { namespace gen {",
        "",
        f"inline constexpr int kSchemaVersion = {int(cfg['schemaVersion'])};",
        "",
        "struct ParamDef {",
        "  const char* shader;",
        "  const char* name;",
        "  float defaultValue;",
        "  float minValue;",
        "  float maxValue;",
        "};",
        "",
        "inline constexpr ParamDef kParamDefs[] = {",
        *rows,
        "};",
        "inline constexpr size_t kParamDefCount = sizeof(kParamDefs) / sizeof(kParamDefs[0]);",
        "",
        "inline const ParamDef* FindParamDef(const char* shader, const char* name) {",
        "  for (size_t i = 0; i < kParamDefCount; ++i) {",
        "    if (std::strcmp(kParamDefs[i].shader, shader) == 0 &&",
        "        std::strcmp(kParamDefs[i].name, name) == 0) return &kParamDefs[i];",
        "  }",
        "  return nullptr;",
        "}",
        "",
        "inline size_t ParamCountForShader(const char* shader) {",
        "  size_t n = 0;",
        "  for (size_t i = 0; i < kParamDefCount; ++i)",
        "    if (std::strcmp(kParamDefs[i].shader, shader) == 0) ++n;",
        "  return n;",
        "}",
        "",
        "}  // namespace gen",
        "}  // namespace fx",
        "",
    ]

    out = ROOT / "include/fx/generated/params.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"generated {out.relative_to(ROOT)} ({len(rows)} params)")


if __name__ == "__main__":
    main()
