cmake_minimum_required(VERSION 3.20)
project(Asphalt5Recomp C)

# UWP / Xbox One build: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/uwp-x64.cmake
option(RECOMP_UWP "Build the UWP / Xbox One app instead of the Win32 game" OFF)
if(RECOMP_UWP)
  enable_language(CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

enable_testing()
add_subdirectory(src)
