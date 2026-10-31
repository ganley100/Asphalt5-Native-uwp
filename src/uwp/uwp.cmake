# UWP / Xbox One app.  Included from src/CMakeLists.txt when RECOMP_UWP is on
# (see cmake/uwp-x64.cmake and docs/XBOX.md).
#
# Output: ${CMAKE_BINARY_DIR}/AppxLayout, a complete package layout that
# tools/uwp/package.ps1 turns into a signed .appx with makeappx/signtool.

# ---- ANGLE (OpenGL ES on Direct3D 11, UWP build) --------------------------
set(ANGLE_NUPKG_URL "https://www.nuget.org/api/v2/package/ANGLE.WindowsStore/2.1.13")
set(ANGLE_NUPKG_SHA256 "d8f6c2bf2f6069bc2c1f77548140cf7c9dccd3aff79b15c5eb40c0e8f4032977")
set(ANGLE_DIR "" CACHE PATH "Extracted ANGLE.WindowsStore 2.1.13 package (downloaded when empty)")
if(NOT ANGLE_DIR)
  set(ANGLE_DIR "${CMAKE_BINARY_DIR}/angle")
  if(NOT EXISTS "${ANGLE_DIR}/bin/UAP/x64/libGLESv2.dll")
    message(STATUS "Downloading ANGLE.WindowsStore 2.1.13")
    file(DOWNLOAD "${ANGLE_NUPKG_URL}" "${CMAKE_BINARY_DIR}/angle.nupkg"
         EXPECTED_HASH SHA256=${ANGLE_NUPKG_SHA256} STATUS _angle_status)
    list(GET _angle_status 0 _angle_rc)
    if(NOT _angle_rc EQUAL 0)
      message(FATAL_ERROR "ANGLE download failed: ${_angle_status}. Set ANGLE_DIR to an extracted package.")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/angle.nupkg" DESTINATION "${ANGLE_DIR}")
  endif()
endif()
set(ANGLE_BIN "${ANGLE_DIR}/bin/UAP/x64")

# ---- core: runtime, translation, host and the GLES fixed-function layer ---
add_library(asphalt5_core STATIC
  core/guest.c
  core/imports.c
  core/jni.c
  platform/win32.c
  platform/audio.c
  platform/stb_vorbis.c
  platform/movie.c
  gl/gl_globals.c
  gl/gl_state.c
  gl/gl_matrix.c
  gl/gl_texture.c
  gl/gl_light.c
  gl/gl_arrays.c
  gl/gl_draw.c
  gl/gl_query.c
  gl/gl_capture.c
  gl/gl_diag.c
  gl/gl_dispatch.c
  gl/ff/ff_gles.c
  ${GENERATED_FUNCTION_SOURCES}
  "${GENERATED_DIR}/table.c"
  "${GENERATED_DIR}/imports_gen.c"
  "${GENERATED_DIR}/plt_table.c"
)
target_include_directories(asphalt5_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/gl"
  "${GENERATED_DIR}"
)
target_compile_definitions(asphalt5_core PUBLIC RECOMP_UWP)

set(ASPHALT5_WARNINGS
  -O1 -g -fno-strict-aliasing -fwrapv -Wall -Wextra
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable
  -Wno-unused-label -Wno-unused-value
)
target_compile_options(asphalt5_core PRIVATE ${ASPHALT5_WARNINGS})
target_link_libraries(asphalt5_core PUBLIC
  windowsapp xaudio2_9 mfplat mfreadwrite mfuuid uuid)

add_library(asphalt5_miniz STATIC
  third_party/miniz/miniz.c
  third_party/miniz/miniz_tdef.c
  third_party/miniz/miniz_tinfl.c
  third_party/miniz/miniz_zip.c
)
target_compile_options(asphalt5_miniz PRIVATE -O2 -w)

# ---- the app ---------------------------------------------------------------
set(UWP_LAYOUT "${CMAKE_BINARY_DIR}/AppxLayout")
add_executable(Asphalt5Uwp WIN32
  main.c
  uwp/uwp_app.cpp
  uwp/uwp_import.c
)
target_compile_definitions(Asphalt5Uwp PRIVATE RECOMP_GUI)
target_compile_options(Asphalt5Uwp PRIVATE ${ASPHALT5_WARNINGS})
target_link_libraries(Asphalt5Uwp PRIVATE asphalt5_core asphalt5_miniz)
# AppContainer image, wWinMain entry, a large main-thread stack (translated
# guest calls nest on the host stack), a static C++ runtime, and no debug
# info in the package (68 MB -> 9 MB).
target_link_options(Asphalt5Uwp PRIVATE
  -municode -static -s -Wl,--appcontainer -Wl,--stack,16777216)
set_target_properties(Asphalt5Uwp PROPERTIES
  OUTPUT_NAME "Asphalt5"
  RUNTIME_OUTPUT_DIRECTORY "${UWP_LAYOUT}"
)

# ---- package layout --------------------------------------------------------
set(UWP_VERSION "2.1.0.0" CACHE STRING "Package version (four parts)")
set(UWP_PUBLISHER "CN=Asphalt5Native" CACHE STRING "Package publisher; must match the signing certificate subject")
set(UWP_GAME_DIR "" CACHE PATH
    "Optional folder with your own APK and GloftA5HD data, bundled as game/ (never distribute such a package)")
set(_uwp_src "${CMAKE_SOURCE_DIR}/installer/uwp")
configure_file("${_uwp_src}/AppxManifest.xml.in" "${UWP_LAYOUT}/AppxManifest.xml" @ONLY)
file(COPY "${_uwp_src}/Assets" DESTINATION "${UWP_LAYOUT}")
file(COPY "${ANGLE_BIN}/libEGL.dll" "${ANGLE_BIN}/libGLESv2.dll" DESTINATION "${UWP_LAYOUT}")
file(MAKE_DIRECTORY "${UWP_LAYOUT}/licenses")
configure_file("${ANGLE_DIR}/LICENSE" "${UWP_LAYOUT}/licenses/ANGLE-LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${UWP_LAYOUT}/licenses/LICENSE.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/NOTICE.md" "${UWP_LAYOUT}/licenses/NOTICE.md" COPYONLY)
if(UWP_GAME_DIR)
  message(STATUS "Bundling game files from ${UWP_GAME_DIR} (personal package only)")
  file(GLOB _uwp_apks "${UWP_GAME_DIR}/*.apk")
  file(COPY ${_uwp_apks} DESTINATION "${UWP_LAYOUT}/game")
  if(EXISTS "${UWP_GAME_DIR}/GloftA5HD")
    file(COPY "${UWP_GAME_DIR}/GloftA5HD" DESTINATION "${UWP_LAYOUT}/game")
  endif()
endif()
