cmake_minimum_required(VERSION 3.16)

# CMP0091 lets us pick the static MSVC runtime through
# CMAKE_MSVC_RUNTIME_LIBRARY further down.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# These must be set before project(). The tool targets Apple Silicon only.
# 12.0 is needed for kIOMainPortDefault in sysinfo_mac.cpp.
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

project(xymyx-toolkit VERSION 1.0.0 LANGUAGES CXX)

# Default to an optimised build so "cmake -S . -B build" just works.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# /MT: put the C++ runtime inside the .exe. Otherwise the tool would need the
# "Visual C++ Redistributable" installed on every PC the stick is plugged into.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Shared code: compiled on every platform.
file(GLOB XYMYX_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cpp
  ${CMAKE_SOURCE_DIR}/src/core/*.cpp
  ${CMAKE_SOURCE_DIR}/src/features/*.cpp)

# Platform layer: exactly one folder is compiled. This is what keeps
# #ifdef out of the feature code.
if(WIN32)
  file(GLOB XYMYX_PLATFORM CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp)
elseif(APPLE)
  file(GLOB XYMYX_PLATFORM CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/mac/*.cpp)
else()
  message(FATAL_ERROR "XYMYX Toolkit supports Windows and macOS only")
endif()

add_executable(xymyx-toolkit ${XYMYX_SOURCES} ${XYMYX_PLATFORM})
target_include_directories(xymyx-toolkit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(xymyx-toolkit PRIVATE XYMYX_VERSION="${PROJECT_VERSION}")

# Always put the executable in <build>/bin, whatever generator is used.
set_target_properties(xymyx-toolkit PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
  # UNICODE: use the wide-character (W) versions of the Windows API.
  # NOMINMAX: stop windows.h from defining min/max macros that break std::min.
  # _WIN32_WINNT=0x0A00: we target Windows 10 and newer.
  target_compile_definitions(xymyx-toolkit PRIVATE
    UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
  # System libraries that ship with every Windows. Nothing here needs installing.
  target_link_libraries(xymyx-toolkit PRIVATE
    iphlpapi ws2_32 netapi32 ole32 oleaut32 shell32 propsys advapi32 user32)
  if(MSVC)
    # /utf-8: treat source files and string literals as UTF-8.
    target_compile_options(xymyx-toolkit PRIVATE /W4 /utf-8 /EHsc)
  else()
    target_compile_options(xymyx-toolkit PRIVATE -Wall -Wextra)
    # MinGW: link libstdc++ and libgcc into the .exe so no extra DLLs are needed.
    target_link_options(xymyx-toolkit PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
elseif(APPLE)
  target_compile_options(xymyx-toolkit PRIVATE -Wall -Wextra)
  # macOS does not support fully static executables. System frameworks are
  # present on every Mac, so linking them keeps the tool self-contained.
  target_link_libraries(xymyx-toolkit PRIVATE "-framework CoreFoundation" "-framework IOKit")
endif()
