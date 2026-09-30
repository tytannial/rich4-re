cmake_minimum_required(VERSION 3.24)
project(rich4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 优先使用 third_party 下的 SDL3 预编译包（Windows）；否则 find_package（Linux 发行版
# sdl3 包）；再否则 FetchContent 拉取源码。
if(NOT SDL3_DIR AND WIN32)
  file(GLOB _sdl3_candidates "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3-*")
  foreach(_dir ${_sdl3_candidates})
    if(EXISTS "${_dir}/cmake/SDL3Config.cmake")
      set(SDL3_DIR "${_dir}/cmake")
    endif()
  endforeach()
endif()

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found; fetching from source")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
else()
  message(STATUS "Using SDL3 at ${SDL3_DIR}")
endif()

# FreeType（文本渲染；系统包优先 → 本地 third_party → FetchContent）
# 依据: GDI 文本渲染为 Windows 独占（docs/cross-platform.md 阶段4）→ FreeType 统一两平台；
#       FT_DISABLE_* 关闭可选依赖（zlib/bzip2/png/harfbuzz/brotli）走纯 C 无依赖构建
find_package(Freetype QUIET)
if(Freetype_FOUND)
  message(STATUS "Using system FreeType ${FREETYPE_VERSION_STRING}")
  set(RICH4_FREETYPE_TARGET Freetype::Freetype)
else()
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  if(NOT FREETYPE_SRC_DIR)
    file(GLOB _ft_candidates "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype")
    foreach(_dir ${_ft_candidates})
      if(EXISTS "${_dir}/CMakeLists.txt")
        set(FREETYPE_SRC_DIR "${_dir}")
      endif()
    endforeach()
  endif()

  if(FREETYPE_SRC_DIR)
    message(STATUS "Using FreeType at ${FREETYPE_SRC_DIR}")
    add_subdirectory("${FREETYPE_SRC_DIR}" "${CMAKE_BINARY_DIR}/freetype")
  else()
    message(STATUS "FreeType not found locally; fetching from source")
    include(FetchContent)
    FetchContent_Declare(freetype
      GIT_REPOSITORY https://github.com/freetype/freetype.git
      GIT_TAG VER-2-14-3
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype)
  endif()
  set(RICH4_FREETYPE_TARGET freetype)
endif()

file(GLOB_RECURSE RICH4_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM RICH4_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

# [NEW] rich4_core：游戏引擎全部源文件（不含 main），供 rich4 可执行与单测共用
add_library(rich4_core STATIC ${RICH4_SOURCES})
target_include_directories(rich4_core PUBLIC "${CMAKE_SOURCE_DIR}/include"
                                             "${CMAKE_SOURCE_DIR}/third_party/stb")
target_link_libraries(rich4_core PUBLIC SDL3::SDL3 ${RICH4_FREETYPE_TARGET})
if(MSVC)
  target_compile_options(rich4_core PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(rich4_core PRIVATE -Wall -Wextra)
endif()

add_executable(rich4 "${CMAKE_SOURCE_DIR}/src/main.cpp")
target_link_libraries(rich4 PRIVATE rich4_core)

if(MSVC)
  target_compile_options(rich4 PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(rich4 PRIVATE -Wall -Wextra)
endif()

# ---- 单元测试（L0，docs/testing.md）----
enable_testing()
file(GLOB RICH4_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/unit/*.cpp")
if(RICH4_TEST_SOURCES)
  add_executable(rich4_tests ${RICH4_TEST_SOURCES})
  target_link_libraries(rich4_tests PRIVATE rich4_core)
  if(MSVC)
    target_compile_options(rich4_tests PRIVATE /W4 /utf-8 /permissive-)
  else()
    target_compile_options(rich4_tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME unit COMMAND rich4_tests)
endif()

# 预编译包为动态库，运行前复制 SDL3.dll 到输出目录。
if(WIN32)
  file(GLOB _sdl3_dll "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3-*/lib/x64/SDL3.dll")
  if(_sdl3_dll)
    add_custom_command(TARGET rich4 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sdl3_dll} "$<TARGET_FILE_DIR:rich4>")
  endif()
endif()
