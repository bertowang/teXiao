#!/usr/bin/env python3
"""生成可直接用 Xcode 打开的 demo 工程（零依赖，不需要 CMake）。

  python3 tools/gen_xcodeproj.py          # 同时生成 iOS 与 macOS 两个工程
  python3 tools/gen_xcodeproj.py ios     # 仅 TeXiaoDemo.xcodeproj
  python3 tools/gen_xcodeproj.py mac     # 仅 TeXiaoMacDemo.xcodeproj

增删源文件后重跑本脚本即可，避免手工维护 pbxproj。两个 target 共用
src/core、src/render、src/effects、src/ai 与 src/platform/apple、src/platform/metal。
"""
import glob
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMON_GLOBS = [
    "src/core/*.cpp", "src/render/*.cpp", "src/effects/*.cpp", "src/ai/*.cpp",
    "src/platform/metal/*.mm", "src/platform/apple/*.mm",
]

APPLE_FRAMEWORKS = ["Foundation", "AVFoundation", "CoreVideo", "CoreMedia",
                    "Metal", "QuartzCore", "CoreGraphics"]

SPECS = {
    "ios": {
        "project": "TeXiaoDemo.xcodeproj",
        "target": "TeXiaoDemo",
        "bundle_id": "com.texiao.demo",
        "frameworks": APPLE_FRAMEWORKS + ["UIKit"],
        "globs": COMMON_GLOBS + ["src/platform/ios/*.mm", "src/platform/ios/App/*.mm"],
        "plist": "src/platform/ios/App/Info.plist",
        "sdkroot": "iphoneos",
        "platforms": "iphoneos iphonesimulator",
        "deployment_key": "IPHONEOS_DEPLOYMENT_TARGET",
        "deployment": "15.0",
        "extra": {
            "TARGETED_DEVICE_FAMILY": '"1,2"',
            "ENABLE_BITCODE": "NO",
            "ASSETCATALOG_COMPILER_GENERATE_SWIFT_ASSET_SYMBOL_EXTENSIONS": "YES",
            "UILAUNCHSCREEN_GENERATION": "YES",
        },
    },
    "mac": {
        "project": "TeXiaoMacDemo.xcodeproj",
        "target": "TeXiaoMacDemo",
        "bundle_id": "com.texiao.macdemo",
        "frameworks": APPLE_FRAMEWORKS + ["AppKit"],
        "globs": COMMON_GLOBS + ["src/platform/mac/*.mm", "src/platform/mac/App/*.mm"],
        "plist": "src/platform/mac/App/Info.plist",
        "sdkroot": "macosx",
        "platforms": "macosx",
        "deployment_key": "MACOSX_DEPLOYMENT_TARGET",
        "deployment": "12.0",
        "extra": {
            "COMBINE_HIDPI_IMAGES": "YES",
        },
    },
}


def uid(spec, key):
    return hashlib.md5((spec + ":" + key).encode("utf-8")).hexdigest()[:24].upper()


def collect(spec):
    sources = []
    for pattern in spec["globs"]:
        sources += sorted(glob.glob(pattern, root_dir=ROOT))
    resources = sorted(glob.glob("assets/*.json", root_dir=ROOT))
    return sources, resources


def file_type(path):
    if path.endswith(".mm"):
        return "sourcecode.cpp.objcpp"
    if path.endswith(".cpp"):
        return "sourcecode.cpp.cpp"
    if path.endswith(".json"):
        return "text.json"
    return "text"


def quote(value):
    """pbxproj 字符串：含 / . - 等字符时加引号更稳妥。"""
    safe = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-+")
    return value if value and all(c in safe for c in value) else '"' + value + '"'


def generate(name, spec):
    project_dir = ROOT / spec["project"]
    target_name = spec["target"]
    frameworks = spec["frameworks"]
    sources, resources = collect(spec)
    if not sources:
        raise SystemExit(f"no source files found for {name}")

    ids = {}
    for path in sources + resources:
        ids[path] = {"ref": uid(name, "ref:" + path), "build": uid(name, "build:" + path)}
    for fw in frameworks:
        ids[fw] = {"ref": uid(name, "fwref:" + fw), "build": uid(name, "fwbuild:" + fw)}
    fixed = {
        key: uid(name, key) for key in
        ["project", "target", "mainGroup", "sourcesGroup", "resourcesGroup",
         "frameworksGroup", "productsGroup", "productRef", "sourcesPhase",
         "resourcesPhase", "frameworksPhase", "projConfigList", "targetConfigList",
         "projDebug", "projRelease", "targetDebug", "targetRelease"]
    }

    out = []
    add = out.append
    add("// !$*UTF8*$!")
    add("{")
    add("\tarchiveVersion = 1;")
    add("\tclasses = {\n\t};")
    add("\tobjectVersion = 56;")
    add("\tobjects = {")

    # PBXBuildFile
    add("\n/* Begin PBXBuildFile section */")
    for path in sources + resources:
        add(f"\t\t{ids[path]['build']} /* {path} in Sources/Resources */ = "
            f"{{isa = PBXBuildFile; fileRef = {ids[path]['ref']} /* {path} */; }};")
    for fw in frameworks:
        add(f"\t\t{ids[fw]['build']} /* {fw}.framework in Frameworks */ = "
            f"{{isa = PBXBuildFile; fileRef = {ids[fw]['ref']} /* {fw}.framework */; }};")
    add("/* End PBXBuildFile section */")

    # PBXFileReference
    add("\n/* Begin PBXFileReference section */")
    for path in sources + resources:
        add(f"\t\t{ids[path]['ref']} /* {path} */ = {{isa = PBXFileReference; "
            f"lastKnownFileType = {file_type(path)}; path = {quote(path)}; "
            f"sourceTree = SOURCE_ROOT; }};")
    for fw in frameworks:
        framework_name = fw + ".framework"
        add(f"\t\t{ids[fw]['ref']} /* {framework_name} */ = {{isa = PBXFileReference; "
            f"lastKnownFileType = wrapper.framework; name = {quote(framework_name)}; "
            f"path = {quote('System/Library/Frameworks/' + framework_name)}; sourceTree = SDKROOT; }};")
    add(f"\t\t{fixed['productRef']} /* {target_name}.app */ = {{isa = PBXFileReference; "
        f"explicitFileType = wrapper.application; includeInIndex = 0; "
        f"path = {target_name}.app; sourceTree = BUILT_PRODUCTS_DIR; }};")
    add("/* End PBXFileReference section */")

    # PBXFrameworksBuildPhase
    add("\n/* Begin PBXFrameworksBuildPhase section */")
    add(f"\t\t{fixed['frameworksPhase']} /* Frameworks */ = {{")
    add("\t\t\tisa = PBXFrameworksBuildPhase;")
    add("\t\t\tbuildActionMask = 2147483647;")
    add("\t\t\tfiles = (")
    for fw in frameworks:
        add(f"\t\t\t\t{ids[fw]['build']} /* {fw}.framework in Frameworks */,")
    add("\t\t\t);")
    add("\t\t\trunOnlyForDeploymentPostprocessing = 0;")
    add("\t\t};")
    add("/* End PBXFrameworksBuildPhase section */")

    # PBXGroup
    add("\n/* Begin PBXGroup section */")
    groups = [
        (fixed["mainGroup"], "Main Group",
         [fixed["sourcesGroup"], fixed["resourcesGroup"], fixed["frameworksGroup"],
          fixed["productsGroup"]]),
        (fixed["sourcesGroup"], "Sources", [ids[p]["ref"] for p in sources]),
        (fixed["resourcesGroup"], "Resources", [ids[p]["ref"] for p in resources]),
        (fixed["frameworksGroup"], "Frameworks", [ids[fw]["ref"] for fw in frameworks]),
        (fixed["productsGroup"], "Products", [fixed["productRef"]]),
    ]
    for gid, group_name, children in groups:
        add(f"\t\t{gid} /* {group_name} */ = {{")
        add("\t\t\tisa = PBXGroup;")
        add("\t\t\tchildren = (")
        for child in children:
            add(f"\t\t\t\t{child},")
        add("\t\t\t);")
        add(f"\t\t\tname = {quote(group_name)};")
        add("\t\t\tsourceTree = \"<group>\";")
        add("\t\t};")
    add("/* End PBXGroup section */")

    # PBXNativeTarget
    add("\n/* Begin PBXNativeTarget section */")
    add(f"\t\t{fixed['target']} /* {target_name} */ = {{")
    add("\t\t\tisa = PBXNativeTarget;")
    add(f"\t\t\tbuildConfigurationList = {fixed['targetConfigList']} /* Build configuration list for PBXNativeTarget \"{target_name}\" */;")
    add("\t\t\tbuildPhases = (")
    add(f"\t\t\t\t{fixed['sourcesPhase']} /* Sources */,")
    add(f"\t\t\t\t{fixed['frameworksPhase']} /* Frameworks */,")
    add(f"\t\t\t\t{fixed['resourcesPhase']} /* Resources */,")
    add("\t\t\t);")
    add("\t\t\tbuildRules = (\n\t\t\t);")
    add("\t\t\tdependencies = (\n\t\t\t);")
    add(f"\t\t\tname = {target_name};")
    add(f"\t\t\tproductName = {target_name};")
    add(f"\t\t\tproductReference = {fixed['productRef']} /* {target_name}.app */;")
    add("\t\t\tproductType = \"com.apple.product-type.application\";")
    add("\t\t};")
    add("/* End PBXNativeTarget section */")

    # PBXProject
    add("\n/* Begin PBXProject section */")
    add(f"\t\t{fixed['project']} /* Project object */ = {{")
    add("\t\t\tisa = PBXProject;")
    add("\t\t\tattributes = {")
    add("\t\t\t\tBuildIndependentTargetsInParallel = 1;")
    add("\t\t\t\tLastUpgradeCheck = 2720;")
    add(f"\t\t\t\tTargetAttributes = {{\n\t\t\t\t\t{fixed['target']} = {{\n"
        "\t\t\t\t\t\tCreatedOnToolsVersion = 27.2;\n"
        "\t\t\t\t\t\tProvisioningStyle = Automatic;\n"
        "\t\t\t\t\t};\n\t\t\t\t};")
    add("\t\t\t};")
    add(f"\t\t\tbuildConfigurationList = {fixed['projConfigList']} /* Build configuration list for PBXProject \"{target_name}\" */;")
    add("\t\t\tcompatibilityVersion = \"Xcode 14.0\";")
    add("\t\t\tdevelopmentRegion = en;")
    add("\t\t\thasScannedForEncodings = 0;")
    add("\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);")
    add(f"\t\t\tmainGroup = {fixed['mainGroup']};")
    add(f"\t\t\tproductRefGroup = {fixed['productsGroup']} /* Products */;")
    add("\t\t\tprojectDirPath = \"\";")
    add("\t\t\tprojectRoot = \"\";")
    add(f"\t\t\ttargets = (\n\t\t\t\t{fixed['target']} /* {target_name} */,\n\t\t\t);")
    add("\t\t};")
    add("/* End PBXProject section */")

    # PBXResourcesBuildPhase / PBXSourcesBuildPhase
    add("\n/* Begin PBXResourcesBuildPhase section */")
    add(f"\t\t{fixed['resourcesPhase']} /* Resources */ = {{")
    add("\t\t\tisa = PBXResourcesBuildPhase;")
    add("\t\t\tbuildActionMask = 2147483647;")
    add("\t\t\tfiles = (")
    for path in resources:
        add(f"\t\t\t\t{ids[path]['build']} /* {path} in Resources */,")
    add("\t\t\t);")
    add("\t\t\trunOnlyForDeploymentPostprocessing = 0;")
    add("\t\t};")
    add("/* End PBXResourcesBuildPhase section */")

    add("\n/* Begin PBXSourcesBuildPhase section */")
    add(f"\t\t{fixed['sourcesPhase']} /* Sources */ = {{")
    add("\t\t\tisa = PBXSourcesBuildPhase;")
    add("\t\t\tbuildActionMask = 2147483647;")
    add("\t\t\tfiles = (")
    for path in sources:
        add(f"\t\t\t\t{ids[path]['build']} /* {path} in Sources */,")
    add("\t\t\t);")
    add("\t\t\trunOnlyForDeploymentPostprocessing = 0;")
    add("\t\t};")
    add("/* End PBXSourcesBuildPhase section */")

    # XCBuildConfiguration
    common_project = {
        "ALWAYS_SEARCH_USER_PATHS": "NO",
        "CLANG_ANALYZER_NONNULL": "YES",
        "CLANG_ENABLE_MODULES": "YES",
        "CLANG_ENABLE_OBJC_ARC": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": '"c++17"',
        "CLANG_CXX_LIBRARY": '"libc++"',
        "COPY_PHASE_STRIP": "NO",
        "ENABLE_STRICT_OBJC_MSGSEND": "YES",
        spec["deployment_key"]: spec["deployment"],
        "SDKROOT": spec["sdkroot"],
        "SUPPORTED_PLATFORMS": quote(spec["platforms"]),
    }
    common_target = {
        "CLANG_ENABLE_OBJC_ARC": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": '"c++17"',
        "CLANG_CXX_LIBRARY": '"libc++"',
        "CODE_SIGN_STYLE": "Automatic",
        "CURRENT_PROJECT_VERSION": "1",
        "GENERATE_INFOPLIST_FILE": "NO",
        "HEADER_SEARCH_PATHS": '(\n\t\t\t\t\t"$(SRCROOT)/include",\n\t\t\t\t\t"$(SRCROOT)/src",\n\t\t\t\t)',
        "INFOPLIST_FILE": quote(spec["plist"]),
        spec["deployment_key"]: spec["deployment"],
        "LD_RUNPATH_SEARCH_PATHS": '(\n\t\t\t\t\t"$(inherited)",\n\t\t\t\t\t"@executable_path/Frameworks",\n\t\t\t\t)',
        "MARKETING_VERSION": "0.1.0",
        "MTL_ENABLE_DEBUG_INFO": "INCLUDE_SOURCE",
        "MTL_FAST_MATH": "YES",
        "PRODUCT_BUNDLE_IDENTIFIER": spec["bundle_id"],
        "PRODUCT_NAME": '"$(TARGET_NAME)"',
        "SDKROOT": spec["sdkroot"],
        "SUPPORTED_PLATFORMS": quote(spec["platforms"]),
        "SWIFT_EMIT_LOC_STRINGS": "YES",
    }
    common_target.update(spec["extra"])

    def config_block(cid, config_name, settings):
        add(f"\t\t{cid} /* {config_name} */ = {{")
        add("\t\t\tisa = XCBuildConfiguration;")
        add("\t\t\tbuildSettings = {")
        for key, value in settings.items():
            add(f"\t\t\t\t{key} = {value};")
        add("\t\t\t};")
        add(f"\t\t\tname = {config_name};")
        add("\t\t};")

    add("\n/* Begin XCBuildConfiguration section */")
    proj_debug = dict(common_project)
    proj_debug.update({"ENABLE_TESTABILITY": "YES", "GCC_OPTIMIZATION_LEVEL": "0",
                       "GCC_PREPROCESSOR_DEFINITIONS": '(\n\t\t\t\t\t"DEBUG=1",\n\t\t\t\t\t"$(inherited)",\n\t\t\t\t)',
                       "ONLY_ACTIVE_ARCH": "YES"})
    proj_release = dict(common_project)
    proj_release.update({"ENABLE_NS_ASSERTIONS": "NO", "ONLY_ACTIVE_ARCH": "NO",
                         "VALIDATE_PRODUCT": "YES"})
    config_block(fixed["projDebug"], "Debug", proj_debug)
    config_block(fixed["projRelease"], "Release", proj_release)

    target_debug = dict(common_target)
    target_debug["GCC_PREPROCESSOR_DEFINITIONS"] = '(\n\t\t\t\t\t"DEBUG=1",\n\t\t\t\t\t"$(inherited)",\n\t\t\t\t)'
    target_release = dict(common_target)
    config_block(fixed["targetDebug"], "Debug", target_debug)
    config_block(fixed["targetRelease"], "Release", target_release)
    add("/* End XCBuildConfiguration section */")

    # XCConfigurationList
    add("\n/* Begin XCConfigurationList section */")
    for cid, label, debug, release in [
        (fixed["projConfigList"], "PBXProject", fixed["projDebug"], fixed["projRelease"]),
        (fixed["targetConfigList"], "PBXNativeTarget", fixed["targetDebug"], fixed["targetRelease"]),
    ]:
        add(f"\t\t{cid} /* Build configuration list for {label} \"{target_name}\" */ = {{")
        add("\t\t\tisa = XCConfigurationList;")
        add("\t\t\tbuildConfigurations = (")
        add(f"\t\t\t\t{debug} /* Debug */,")
        add(f"\t\t\t\t{release} /* Release */,")
        add("\t\t\t);")
        add("\t\t\tdefaultConfigurationIsVisible = 0;")
        add("\t\t\tdefaultConfigurationName = Release;")
        add("\t\t};")
    add("/* End XCConfigurationList section */")

    add("\t};")
    add(f"\trootObject = {fixed['project']} /* Project object */;")
    add("}")

    project_dir.mkdir(parents=True, exist_ok=True)
    (project_dir / "project.pbxproj").write_text("\n".join(out) + "\n", encoding="utf-8")

    scheme_dir = project_dir / "xcshareddata/xcschemes"
    scheme_dir.mkdir(parents=True, exist_ok=True)
    ref = (f'<BuildableReference BuildableIdentifier="primary" '
           f'BlueprintIdentifier="{fixed["target"]}" BuildableName="{target_name}.app" '
           f'BlueprintName="{target_name}" '
           f'ReferencedContainer="container:{target_name}.xcodeproj"/>')
    scheme = f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="2720" version="1.7">
   <BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES">
            {ref}
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv="YES"/>
   <LaunchAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle="0" useCustomWorkingDirectory="NO" ignoresPersistentStateOnLaunch="NO" debugDocumentVersioning="YES" debugServiceExtension="internal" allowLocationSimulation="YES">
      <BuildableProductRunnable runnableDebuggingMode="0">
         {ref}
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction buildConfiguration="Release" shouldUseLaunchSchemeArgsEnv="YES" savedToolIdentifier="" useCustomWorkingDirectory="NO" debugDocumentVersioning="YES">
      <BuildableProductRunnable runnableDebuggingMode="0">
         {ref}
      </BuildableProductRunnable>
   </ProfileAction>
   <AnalyzeAction buildConfiguration="Debug"/>
   <ArchiveAction buildConfiguration="Release" revealArchiveInOrganizer="YES"/>
</Scheme>
"""
    (scheme_dir / f"{target_name}.xcscheme").write_text(scheme, encoding="utf-8")
    print(f"generated {project_dir.name} ({len(sources)} sources, {len(resources)} resources, "
          f"{len(frameworks)} frameworks)")


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which not in ("all", "ios", "mac"):
        raise SystemExit("usage: gen_xcodeproj.py [all|ios|mac]")
    for name, spec in SPECS.items():
        if which in ("all", name):
            generate(name, spec)


if __name__ == "__main__":
    main()
