cmake_minimum_required(VERSION 3.24)
project(rich4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 优先使用 third_party 下的 SDL3 预编译包；否则 FetchContent 拉取源码。
if(NOT SDL3_DIR)
  file(GLOB _sdl3_candidates "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3-*")
  foreach(_dir ${_sdl3_candidates})
    if(EXISTS "${_dir}/cmake/SDL3Config.cmake")
      set(SDL3_DIR "${_dir}/cmake")
    endif()
  endforeach()
endif()

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found locally; fetching from source")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.30
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
else()
  message(STATUS "Using SDL3 at ${SDL3_DIR}")
endif()

file(GLOB_RECURSE RICH4_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(rich4 ${RICH4_SOURCES})
target_include_directories(rich4 PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(rich4 PRIVATE SDL3::SDL3)

if(MSVC)
  target_compile_options(rich4 PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(rich4 PRIVATE -Wall -Wextra)
endif()

# 预编译包为动态库，运行前复制 SDL3.dll 到输出目录。
if(WIN32)
  file(GLOB _sdl3_dll "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL3-*/lib/x64/SDL3.dll")
  if(_sdl3_dll)
    add_custom_command(TARGET rich4 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sdl3_dll} "$<TARGET_FILE_DIR:rich4>")
  endif()
endif()
