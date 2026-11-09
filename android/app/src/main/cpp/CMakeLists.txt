cmake_minimum_required(VERSION 3.22)
project(TeXiaoAndroid LANGUAGES CXX)

# 复用仓库根 CMakeLists 的单一事实源：fxengine + fxjni 都在那里定义。
set(FX_BUILD_ANDROID_APP ON CACHE BOOL "" FORCE)
set(FX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FX_PROFILE "lite" CACHE STRING "" FORCE)

get_filename_component(TEXIAO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_subdirectory("${TEXIAO_ROOT}" "${CMAKE_BINARY_DIR}/texiao-core")
