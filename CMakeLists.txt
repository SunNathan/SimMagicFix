cmake_minimum_required(VERSION 3.21)
project(SimagicFix VERSION 0.4.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(SIMAGICFIX_BUILD_GUI "Build the Win32 GUI" ON)

# ---------------------------------------------------------------------------
# Dependencies (vcpkg manifest mode)
# ---------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

# Optional: official LMU SDK header, used only to compare ABI offsets locally.
set(LMU_SDK_DIR "" CACHE PATH "Optional path to Le Mans Ultimate Support/SharedMemoryInterface")

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(simagicfix_core STATIC
  src/core/paths.cpp
  src/core/state.cpp
  src/core/lmu_reader.cpp
  src/core/mock_shm.cpp
  src/core/probe.cpp
  src/core/telemetry.cpp
  src/core/simpro_hook.cpp
  src/core/autostart.cpp
  src/core/installer.cpp
)

target_include_directories(simagicfix_core PUBLIC src)

target_compile_definitions(simagicfix_core PUBLIC
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
  _CRT_SECURE_NO_WARNINGS
  UNICODE
  _UNICODE)

target_link_libraries(simagicfix_core
  PUBLIC advapi32 shell32 ole32 uuid
  PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(simagicfix_core PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# CLI diagnostics
# ---------------------------------------------------------------------------
add_executable(SimagicFixCLI src/cli/main_cli.cpp)
target_link_libraries(SimagicFixCLI PRIVATE simagicfix_core)
set_target_properties(SimagicFixCLI PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Win32 GUI
# ---------------------------------------------------------------------------
if(SIMAGICFIX_BUILD_GUI)
  add_executable(SimagicFix WIN32
    src/gui/main_gui.cpp
    src/gui/app.rc)
  target_include_directories(SimagicFix PRIVATE ${CMAKE_SOURCE_DIR}/src/gui)
  target_link_libraries(SimagicFix PRIVATE simagicfix_core comctl32 gdi32 user32)
  set_target_properties(SimagicFix PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Hook DLL (injected into SimPro)
# ---------------------------------------------------------------------------
add_library(SimagicFixHook SHARED
  src/hook/hook.cpp
)
target_include_directories(SimagicFixHook PRIVATE src)
target_compile_definitions(SimagicFixHook PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
  _CRT_SECURE_NO_WARNINGS
  UNICODE
  _UNICODE)
set_target_properties(SimagicFixHook PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(SimagicFixHook PRIVATE minhook::minhook version)
target_compile_options(SimagicFixHook PRIVATE
  /W4 /permissive- /utf-8 /Zc:__cplusplus)

# ---------------------------------------------------------------------------
# Optional ABI check against the official LMU SDK header
# ---------------------------------------------------------------------------
if(LMU_SDK_DIR)
  if(NOT EXISTS "${LMU_SDK_DIR}/SharedMemoryInterface.hpp")
    message(FATAL_ERROR
      "LMU_SDK_DIR is set but SharedMemoryInterface.hpp was not found in ${LMU_SDK_DIR}")
  endif()
  add_executable(sdk_check tools/sdk_check.cpp)
  target_include_directories(sdk_check PRIVATE ${LMU_SDK_DIR})
  target_compile_definitions(sdk_check PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE)
  set_target_properties(sdk_check PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(sdk_check PRIVATE
    /W4 /permissive- /utf-8 /Zc:__cplusplus
    "/external:I${LMU_SDK_DIR}" "/external:W0")
endif()

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
if(MSVC)
  foreach(t simagicfix_core SimagicFixCLI)
    target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  endforeach()
  if(TARGET SimagicFix)
    target_compile_options(SimagicFix PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  endif()
endif()

install(TARGETS SimagicFixCLI RUNTIME DESTINATION .)
install(TARGETS SimagicFixHook RUNTIME DESTINATION .)
if(TARGET SimagicFix)
  install(TARGETS SimagicFix RUNTIME DESTINATION .)
endif()
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
