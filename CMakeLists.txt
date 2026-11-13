cmake_minimum_required(VERSION 3.21)
project(SimagicFix VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(SIMAGICFIX_BUILD_GUI "Build the Win32 GUI" ON)

# ---------------------------------------------------------------------------
# LMU SDK (official header shipped with the game)
# ---------------------------------------------------------------------------
set(LMU_SDK_DIR "" CACHE PATH "Path to Le Mans Ultimate Support/SharedMemoryInterface")
if(NOT LMU_SDK_DIR)
  set(_lmu_candidates
    "$ENV{ProgramFiles}/Steam/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
    "C:/Program Files (x86)/Steam/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
    "C:/Program Files/Steam/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
    "D:/SteamLibrary/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
    "E:/SteamLibrary/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface")
  foreach(_candidate IN LISTS _lmu_candidates)
    if(EXISTS "${_candidate}/SharedMemoryInterface.hpp")
      set(LMU_SDK_DIR "${_candidate}" CACHE PATH "Path to Le Mans Ultimate Support/SharedMemoryInterface" FORCE)
      break()
    endif()
  endforeach()
endif()

# Fallback used by CI and machines without Le Mans Ultimate installed.
if(NOT LMU_SDK_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/lmu_sdk/SharedMemoryInterface.hpp")
  set(LMU_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/lmu_sdk"
      CACHE PATH "Path to Le Mans Ultimate Support/SharedMemoryInterface" FORCE)
  message(STATUS "LMU SDK: using clean-room fallback header")
endif()
if(NOT LMU_SDK_DIR OR NOT EXISTS "${LMU_SDK_DIR}/SharedMemoryInterface.hpp")
  message(FATAL_ERROR
    "LMU SDK not found. Configure with -DLMU_SDK_DIR=\"C:/.../Le Mans Ultimate/Support/SharedMemoryInterface\"")
endif()
message(STATUS "LMU SDK: ${LMU_SDK_DIR}")

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(simagicfix_core STATIC
  src/core/paths.cpp
  src/core/state.cpp
  src/core/lmu_reader.cpp
  src/core/mock_shm.cpp
  src/core/probe.cpp
  src/core/simpro_client.cpp
  src/core/merge.cpp
  src/core/bridge.cpp
  src/core/simpro_memory.cpp
  src/core/dash_patcher.cpp
  src/core/autostart.cpp
  src/core/installer.cpp
)

target_include_directories(simagicfix_core
  PUBLIC  src
  PRIVATE ${LMU_SDK_DIR} ${CMAKE_SOURCE_DIR}/third_party)

target_compile_definitions(simagicfix_core PUBLIC
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00
  _CRT_SECURE_NO_WARNINGS
  UNICODE
  _UNICODE)

target_link_libraries(simagicfix_core PUBLIC
  ws2_32 advapi32 shell32 ole32 uuid winmm)

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
# Warnings
# ---------------------------------------------------------------------------
if(MSVC)
  foreach(t simagicfix_core SimagicFixCLI)
    target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  endforeach()
  if(TARGET SimagicFix)
    target_compile_options(SimagicFix PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  endif()
  target_compile_options(simagicfix_core PRIVATE
    "/external:I${CMAKE_SOURCE_DIR}/third_party" "/external:W0")
endif()

install(TARGETS SimagicFixCLI RUNTIME DESTINATION .)
if(TARGET SimagicFix)
  install(TARGETS SimagicFix RUNTIME DESTINATION .)
endif()
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
