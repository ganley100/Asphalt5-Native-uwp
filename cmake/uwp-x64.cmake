# Cross toolchain for the UWP / Xbox One build (llvm-mingw).
#
#   cmake -S . -B build-uwp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/uwp-x64.cmake
#
# llvm-mingw's *-mingw32uwp-* wrappers compile against the Windows Store API
# partition (WINAPI_FAMILY_APP) and link the app-safe CRT (ucrtapp).  Works
# from Linux and from Windows (https://github.com/mstorsjo/llvm-mingw).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32uwp)
if(DEFINED ENV{LLVM_MINGW})
  set(_prefix "$ENV{LLVM_MINGW}/bin/")
else()
  set(_prefix "")
endif()
set(CMAKE_C_COMPILER "${_prefix}${_triple}-clang")
set(CMAKE_CXX_COMPILER "${_prefix}${_triple}-clang++")
set(CMAKE_RC_COMPILER "${_prefix}${_triple}-windres")

set(RECOMP_UWP ON CACHE BOOL "Build the UWP / Xbox One app" FORCE)
