# Xbox One (UWP)

The UWP build runs the same static recompilation of Asphalt 5 HD **3.2.4** as
a Universal Windows app. It targets Xbox One / Series consoles in
**Developer Mode**, and also runs as an app on Windows 10/11.

> **Status: experimental.** The build compiles, links and passes the
> automated checks below. It has not yet been run on console hardware. Please
> report what works and attach `asphalt5.log` (see Troubleshooting).

## What is different from the Windows build

| Area | Windows build | UWP / Xbox build |
|---|---|---|
| Window | Win32 + WGL | CoreWindow (`src/uwp/uwp_app.cpp`) |
| Rendering | desktop OpenGL 1.x | OpenGL ES through ANGLE on Direct3D 11; the fixed-function pipeline is emulated with generated shaders (`src/gl/ff/`) |
| Audio | WinMM `waveOut` | XAudio2 |
| Controller | XInput | `Windows.Gaming.Input` |
| Menus | mouse | controller cursor (left stick + A), mouse/touch also work |
| Files | next to `Asphalt5.exe` | the app's `LocalState` folder |
| Toolchain | MinGW GCC | llvm-mingw (`x86_64-w64-mingw32uwp`) |

The translated game code (`generated/`) and the engine glue are shared with
the Windows build.

## 1. Get the package

**From CI:** every push runs the *UWP / Xbox One* workflow. Download the
`Asphalt5Native-UWP-x64` artifact. It contains:

- `Asphalt5Native_x64.appx`
- `Asphalt5Native.cer`
- `Dependencies/` (the VCLibs framework package)

**Build on Windows:**

```powershell
# llvm-mingw (ucrt, x86_64) from https://github.com/mstorsjo/llvm-mingw on PATH, plus CMake + Ninja
cmake -S . -B build-uwp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/uwp-x64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-uwp
powershell -ExecutionPolicy Bypass -File tools\uwp\package.ps1 -Layout build-uwp\AppxLayout
```

`package.ps1` needs the Windows 10/11 SDK (for `makeappx` and `signtool`). It
signs the package with a throwaway self-signed certificate.

**Cross-compile on Linux:** the same CMake commands work with the Linux
llvm-mingw release. Set `LLVM_MINGW=/path/to/llvm-mingw` or put its `bin/` on
`PATH`. The result is `build-uwp/AppxLayout/`. Packing it still needs
`makeappx` on Windows.

The configure step downloads ANGLE.WindowsStore 2.1.13 from NuGet and checks
its SHA-256. Pass `-DANGLE_DIR=<extracted package>` to build offline.

## 2. Install on the console

1. Put the console in [Developer Mode](https://learn.microsoft.com/windows/uwp/xbox-apps/devkit-activation).
2. In Dev Home, enable the **Xbox Device Portal** and open it from a PC browser.
3. Under **Home > My games & apps**, choose **Add**.
4. Select `Asphalt5Native_x64.appx`.
5. Choose **Next**, then add `Dependencies/Microsoft.VCLibs.x64.14.00.appx` (skip this if the console already has it).
6. Choose **Start** to install.
7. In Dev Home, select the app, press the **View** button, choose **View details**, and set **App type** to **Game**.

   This step is required. Apps of type "App" are limited to about 1 GB of
   memory and a share of the GPU, which is not enough for the game.

## 3. Provide your game files

The package never contains game files. On first launch the app looks for
your APK and data in these places, in this order:

1. **LocalState.** In the Device Portal, open **File explorer**, then
   `LocalAppData/Asphalt5Native_…/LocalState`. Upload:
   - your `Asphalt5 … 3.2.4 ….apk`
   - the `GloftA5HD` folder, with its files. Uploading into LocalState also
     works for the folder.
2. **USB drive.** Use a folder named `Asphalt5` on an NTFS or exFAT drive, for
   example `E:\Asphalt5\`. Put the APK and the `GloftA5HD` folder inside it.
   This relies on the `broadFileSystemAccess` capability. If the drive is not
   found, use option 1.
3. **Bundled at build time.** For a personal package only, configure with
   `-DUWP_GAME_DIR=<folder with the APK and GloftA5HD>`. Never share such a
   package.

The app unpacks the APK's `lib/armeabi-v7a/libasphalt5.so`, `res/` and
`assets/` into `LocalState\engine\apk\`. It copies the data to
`LocalState\gamefiles\GloftA5HD\` and shows a progress bar while it works.
This happens only once. Saves are written to the same LocalState folder.

Only the **3.2.4 Sony Ericsson** APK matches the translation in `generated/`.
The 3.3.7 and 3.4.1 builds need their own translation and their PVRTC
textures, which the GLES layer does not decode yet.

## Controls

| Controller | Menus | Race |
|---|---|---|
| Left stick | move the cursor | steer |
| A | touch at the cursor (hold to drag) | nitro |
| B | back (in the pause menu: resume) | brake |
| X | - | tap the screen centre (intros, tutorials) |
| Y | - | camera |
| D-pad | the game's own menu keys | steer / accelerate / brake |
| Right trigger | - | accelerate |
| Left trigger | - | brake |
| Menu / View | pause / resume | pause / resume |

A USB keyboard and mouse work as on Windows.

## Troubleshooting

- **Log:** `LocalState\Asphalt5\asphalt5.log`. Download it from the Device
  Portal file explorer. It records EGL/ANGLE start-up, the importer, the
  generated GLES programs (set `RECOMP_FF_LOG=1` in a local build for more
  detail), and crashes.
- **Black screen, then exit:** check the log for `[uwp] EGL` and `[ff]` lines.
  If `libEGL.dll` or `libGLESv2.dll` failed to load, the VCLibs dependency is
  missing. Install it from `Dependencies/`.
- **"Game files needed" screen:** read the message. Usually the APK is not
  3.2.4, or the `GloftA5HD` folder lacks `package_general.bar_*.cnk`.
- **Out of memory / crash while loading a race:** set the app type to **Game**
  (step 2.7).

## Tests

The OpenGL ES fixed-function layer has pixel tests that run on Mesa
(llvmpipe). CI runs them on every push:

```sh
gcc -O1 -DAPIENTRY= -Isrc/gl/ff tests/ff_gles_test.c src/gl/ff/ff_gles.c -lEGL -lm -o ff_gles_test
EGL_PLATFORM=surfaceless ./ff_gles_test
```

The tests cover colour and colour arrays, `GL_FIXED` vertices, matrix
stacks, `glOrtho`, the MODULATE/REPLACE/BLEND/COMBINE texture environments
with RGBA, ALPHA and LUMINANCE textures, alpha test, linear fog, directional
lighting, colour material, indexed draws, the immediate-mode movie quad, and
the overlay used for the controller cursor.
