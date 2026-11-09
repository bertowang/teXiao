cmake_minimum_required(VERSION 3.25)
project(TeXiao VERSION 0.1.0 LANGUAGES CXX)

# 内核是纯 C++17；iOS / macOS 宿主用 Objective-C++ 门面，构建 App 时才启用 OBJCXX。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FX_BUILD_TESTS "Build headless unit tests" ON)
option(FX_BUILD_IOS_APP "Build the iOS UIKit demo app" OFF)
option(FX_BUILD_MAC_APP "Build the macOS AppKit demo app" OFF)
set(FX_PROFILE "lite" CACHE STRING "Feature profile: min / lite / full")
set_property(CACHE FX_PROFILE PROPERTY STRINGS min lite full)

set(FX_ENGINE_SOURCES
  src/core/frame.cpp
  src/core/json.cpp
  src/core/log.cpp
  src/core/timeline.cpp
  src/core/command_queue.cpp
  src/core/state_store.cpp
  src/core/profiler.cpp
  src/core/scheduler.cpp
  src/core/asset_loader.cpp
  src/core/engine.cpp
  src/render/pass.cpp
  src/render/framebuffer_pool.cpp
  src/render/normalize_pass.cpp
  src/render/render_graph.cpp
  src/render/resource_manager.cpp
  src/render/procedural_textures.cpp
  src/effects/shader_pass.cpp
  src/effects/register_effects.cpp
  src/effects/copy_pass.cpp
  src/effects/lut_pass.cpp
  src/effects/adjust_pass.cpp
  src/effects/beauty_pass.cpp
  src/effects/blur_pass.cpp
  src/effects/sticker_pass.cpp
  src/effects/blend_pass.cpp
  src/ai/detector_hub.cpp
)

# Apple 共用层：iOS 与 macOS 只各写一份 App 壳，Metal / CVPixelBuffer / OC 门面完全复用。
set(FX_APPLE_SOURCES
  src/platform/metal/metal_device.mm
  src/platform/apple/apple_platform.mm
  src/platform/apple/FXEngineFacade.mm
  src/platform/apple/FXAssetCatalog.mm
)

set(FX_IOS_APP_SOURCES
  src/platform/ios/App/main.mm
  src/platform/ios/App/AppDelegate.mm
  src/platform/ios/App/FXPreviewView.mm
  src/platform/ios/App/FXCameraController.mm
  src/platform/ios/App/ViewController.mm
)

set(FX_MAC_APP_SOURCES
  src/platform/mac/App/main.mm
  src/platform/mac/App/AppDelegate.mm
  src/platform/mac/App/FXMetalView.mm
  src/platform/mac/App/FXMacCameraController.mm
  src/platform/mac/App/FXMacViewController.mm
)

set(FX_ASSET_FILES
  assets/demo_identity.json
  assets/demo_lut.json
  assets/demo_beauty.json
  assets/demo_blur.json
  assets/demo_sticker.json
)

if(FX_BUILD_IOS_APP OR FX_BUILD_MAC_APP)
  enable_language(OBJCXX)
  # Metal / CVPixelBuffer 包装依赖 ARC 管理 ObjC 对象生命周期。
  set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES")
  list(APPEND FX_ENGINE_SOURCES ${FX_APPLE_SOURCES})

  # AppKit 只在 macOS 需要；其余框架两端共用。
  set(FX_APPLE_FRAMEWORK_NAMES
    Foundation AVFoundation CoreVideo CoreMedia Metal QuartzCore CoreGraphics)
  if(FX_BUILD_IOS_APP)
    list(APPEND FX_APPLE_FRAMEWORK_NAMES UIKit)
  endif()
  if(FX_BUILD_MAC_APP)
    list(APPEND FX_APPLE_FRAMEWORK_NAMES AppKit)
  endif()
  list(REMOVE_DUPLICATES FX_APPLE_FRAMEWORK_NAMES)
  set(FX_APPLE_FRAMEWORKS "")
  foreach(FW ${FX_APPLE_FRAMEWORK_NAMES})
    find_library(FX_FRAMEWORK_${FW} ${FW})
    if(FX_FRAMEWORK_${FW})
      list(APPEND FX_APPLE_FRAMEWORKS ${FX_FRAMEWORK_${FW}})
    endif()
  endforeach()
endif()

add_library(fxengine STATIC ${FX_ENGINE_SOURCES})
target_include_directories(fxengine
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(fxengine PRIVATE -Wall -Wextra)
if(FX_APPLE_FRAMEWORKS)
  target_link_libraries(fxengine PUBLIC ${FX_APPLE_FRAMEWORKS})
endif()

if(FX_PROFILE STREQUAL "min")
  target_compile_definitions(fxengine PUBLIC
    FX_WITH_SCRIPT=0 FX_WITH_AI=0 FX_WITH_3D=0 FX_WITH_EXPORT=0)
elseif(FX_PROFILE STREQUAL "lite")
  target_compile_definitions(fxengine PUBLIC
    FX_WITH_SCRIPT=0 FX_WITH_AI=1 FX_WITH_3D=0 FX_WITH_EXPORT=0)
else()
  target_compile_definitions(fxengine PUBLIC
    FX_WITH_SCRIPT=1 FX_WITH_AI=1 FX_WITH_3D=1 FX_WITH_EXPORT=1)
endif()

if(FX_BUILD_IOS_APP)
  # ---- iOS Demo App (UIKit + AVFoundation) ----
  add_executable(TeXiaoDemo MACOSX_BUNDLE ${FX_IOS_APP_SOURCES} ${FX_ASSET_FILES})
  target_link_libraries(TeXiaoDemo PRIVATE fxengine)
  target_include_directories(TeXiaoDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  set_source_files_properties(${FX_ASSET_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(TeXiaoDemo PROPERTIES
    MACOSX_BUNDLE YES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios/App/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.texiao.demo"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.texiao.demo"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17"
    XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
    XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "NO"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_UILAUNCHSCREEN_GENERATION "YES")
endif()

if(FX_BUILD_MAC_APP)
  # ---- macOS Demo App (AppKit + AVFoundation) ----
  add_executable(TeXiaoMacDemo MACOSX_BUNDLE ${FX_MAC_APP_SOURCES} ${FX_ASSET_FILES})
  target_link_libraries(TeXiaoMacDemo PRIVATE fxengine)
  target_include_directories(TeXiaoMacDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  set_source_files_properties(${FX_ASSET_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(TeXiaoMacDemo PROPERTIES
    MACOSX_BUNDLE YES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/App/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.texiao.macdemo"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.texiao.macdemo"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "12.0"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17"
    XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
    XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "NO")
endif()

if(FX_BUILD_TESTS AND NOT (FX_BUILD_IOS_APP OR FX_BUILD_MAC_APP))
  enable_testing()
  file(GLOB FX_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp)
  add_executable(fx_unit_tests
    ${FX_TEST_SOURCES}
    tests/mocks/mock_graphics_device.cpp
    tests/mocks/mock_platform.cpp)
  target_include_directories(fx_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(fx_unit_tests PRIVATE fxengine)
  target_compile_definitions(fx_unit_tests PRIVATE
    FX_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME fx_unit_tests COMMAND fx_unit_tests)
endif()
