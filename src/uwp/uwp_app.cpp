/* UWP application host for Xbox One (Dev Mode) and Windows 10/11.
 *
 * Replaces the Win32 window of the desktop build:
 *   - CoreApplication / IFrameworkView with a CoreWindow
 *   - EGL + OpenGL ES through ANGLE (Direct3D 11), loaded from the package
 *   - Windows.Gaming.Input gamepad, exposed to win32.c as an XInput state
 *   - CoreWindow keyboard and pointer events
 *   - LocalState as the working directory (the only writable folder)
 *
 * The game itself still runs exactly as on Windows: Run() calls the regular
 * recomp_main() (main.c) on the UI thread, and the frame loop pumps the
 * CoreWindow dispatcher from platform_pump().
 *
 * Written against the raw WinRT ABI headers shipped with mingw-w64 so the
 * whole port builds with llvm-mingw, without C++/CX or C++/WinRT.
 */
#define WIN32_LEAN_AND_MEAN
/* mingw-w64's windows.foundation.h specializes IReference for both BYTE and
 * boolean, which are the same C++ type; skip the unused boolean one. */
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.applicationmodel.core.h>
#include <windows.applicationmodel.h>
#include <windows.foundation.collections.h>
#include <windows.gaming.input.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <windows.ui.input.h>
#include <wrl/client.h>

#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "uwp_host.h"

using Microsoft::WRL::ComPtr;
namespace WF = ABI::Windows::Foundation;
namespace WFC = ABI::Windows::Foundation::Collections;
namespace WAC = ABI::Windows::ApplicationModel::Core;
namespace WAA = ABI::Windows::ApplicationModel::Activation;
namespace WUC = ABI::Windows::UI::Core;
namespace WUI = ABI::Windows::UI::Input;
namespace WGI = ABI::Windows::Gaming::Input;
namespace WS = ABI::Windows::Storage;

/* ---- small WinRT helpers ---------------------------------------------- */
struct HStr {
    HSTRING h = nullptr;
    explicit HStr(const wchar_t *s) { WindowsCreateString(s, (UINT32)wcslen(s), &h); }
    ~HStr() { if (h) WindowsDeleteString(h); }
    operator HSTRING() const { return h; }
};

template <class T>
static HRESULT get_factory(const wchar_t *cls, T **out)
{
    return RoGetActivationFactory(HStr(cls), __uuidof(T), (void **)out);
}

/* Generic delegate: ITypedEventHandler / IEventHandler specializations. */
template <class I, class S, class A>
struct Handler : I {
    std::function<HRESULT(S, A)> fn;
    LONG ref = 1;
    explicit Handler(std::function<HRESULT(S, A)> f) : fn(std::move(f)) {}
    virtual ~Handler() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **pp) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(I) || iid == __uuidof(IAgileObject)) {
            *pp = static_cast<I *>(this);
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(S s, A a) override { return fn(s, a); }
};

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

/* ---- host state -------------------------------------------------------- */
static ComPtr<WUC::ICoreWindow> g_window;
static ComPtr<WUC::ICoreDispatcher> g_dispatcher;
static volatile int g_closed;
static int g_escape;
static int g_ptr_have, g_ptr_x, g_ptr_y, g_ptr_down;

static wchar_t g_local[MAX_PATH], g_install[MAX_PATH];

/* ---- EGL / ANGLE ------------------------------------------------------- */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;

#define EGL_NONE 0x3038
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_TRUE 1
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3208
#define EGL_ANGLE_DISPLAY_ALLOW_RENDER_TO_BACK_BUFFER 0x320B
#define EGL_ANGLE_SURFACE_RENDER_TO_BACK_BUFFER 0x320C
#define EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE 0x320F

static struct {
    EGLDisplay (APIENTRY *GetPlatformDisplayEXT)(EGLenum, void *, const EGLint *);
    EGLBoolean (APIENTRY *Initialize)(EGLDisplay, EGLint *, EGLint *);
    EGLBoolean (APIENTRY *ChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
    EGLSurface (APIENTRY *CreateWindowSurface)(EGLDisplay, EGLConfig, void *, const EGLint *);
    EGLContext (APIENTRY *CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    EGLBoolean (APIENTRY *MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    EGLBoolean (APIENTRY *SwapBuffers)(EGLDisplay, EGLSurface);
    EGLBoolean (APIENTRY *SwapInterval)(EGLDisplay, EGLint);
    EGLBoolean (APIENTRY *QuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint *);
    EGLint (APIENTRY *GetError)(void);
    void *(APIENTRY *GetProcAddress)(const char *);
} egl;

static HMODULE g_egl_dll, g_gles_dll;
static EGLDisplay g_dpy;
static EGLSurface g_surf;
static EGLContext g_ctx;
static int g_surf_w, g_surf_h;

extern "C" void *uwp_gl_getproc(const char *name)
{
    void *p = g_gles_dll ? (void *)GetProcAddress(g_gles_dll, name) : nullptr;
    if (!p && egl.GetProcAddress) p = egl.GetProcAddress(name);
    return p;
}

static int egl_init(void)
{
    g_gles_dll = LoadPackagedLibrary(L"libGLESv2.dll", 0);
    g_egl_dll = LoadPackagedLibrary(L"libEGL.dll", 0);
    if (!g_egl_dll || !g_gles_dll) {
        log_line("[uwp] ANGLE DLLs missing from the package (%lu)\n", GetLastError());
        return -1;
    }
#define LOAD(f) *(FARPROC *)&egl.f = GetProcAddress(g_egl_dll, "egl" #f)
    LOAD(Initialize); LOAD(ChooseConfig); LOAD(CreateWindowSurface); LOAD(CreateContext);
    LOAD(MakeCurrent); LOAD(SwapBuffers); LOAD(SwapInterval); LOAD(QuerySurface);
    LOAD(GetError); LOAD(GetProcAddress);
#undef LOAD
    if (!egl.GetProcAddress || !egl.Initialize) return -1;
    *(void **)&egl.GetPlatformDisplayEXT = egl.GetProcAddress("eglGetPlatformDisplayEXT");
    if (!egl.GetPlatformDisplayEXT) return -1;

    const EGLint dpy_attr[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_ANGLE_DISPLAY_ALLOW_RENDER_TO_BACK_BUFFER, EGL_TRUE,
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
        EGL_NONE,
    };
    g_dpy = egl.GetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, nullptr, dpy_attr);
    EGLint major = 0, minor = 0;
    if (!g_dpy || !egl.Initialize(g_dpy, &major, &minor)) {
        log_line("[uwp] eglInitialize failed 0x%x\n", egl.GetError());
        return -1;
    }
    const EGLint cfg_attr[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!egl.ChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        log_line("[uwp] eglChooseConfig failed\n");
        return -1;
    }

    /* ANGLE takes a PropertySet holding the CoreWindow and a fixed render
     * size.  The engine renders at 1920x1080 and ANGLE scales to the TV. */
    ComPtr<IInspectable> props_insp;
    ComPtr<WFC::IPropertySet> props;
    ComPtr<WFC::IMap<HSTRING, IInspectable *>> map;
    if (FAILED(RoActivateInstance(HStr(RuntimeClass_Windows_Foundation_Collections_PropertySet),
                                  &props_insp)) ||
        FAILED(props_insp.As(&props)) || FAILED(props.As(&map)))
        return -1;
    boolean replaced = false;
    map->Insert(HStr(L"EGLNativeWindowTypeProperty"), g_window.Get(), &replaced);
    ComPtr<WF::IPropertyValueStatics> pvs;
    if (SUCCEEDED(get_factory(RuntimeClass_Windows_Foundation_PropertyValue, pvs.GetAddressOf()))) {
        WF::Size size = { 1920.0f, 1080.0f };
        ComPtr<IInspectable> sz;
        if (SUCCEEDED(pvs->CreateSize(size, &sz)))
            map->Insert(HStr(L"EGLRenderSurfaceSizeProperty"), sz.Get(), &replaced);
    }
    const EGLint surf_attr[] = { EGL_ANGLE_SURFACE_RENDER_TO_BACK_BUFFER, EGL_TRUE, EGL_NONE };
    g_surf = egl.CreateWindowSurface(g_dpy, cfg, props_insp.Get(), surf_attr);
    if (!g_surf) {
        log_line("[uwp] eglCreateWindowSurface failed 0x%x\n", egl.GetError());
        return -1;
    }
    for (int ver = 3; ver >= 2 && !g_ctx; ver--) {
        const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, ver, EGL_NONE };
        g_ctx = egl.CreateContext(g_dpy, cfg, nullptr, ctx_attr);
    }
    if (!g_ctx || !egl.MakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
        log_line("[uwp] EGL context failed 0x%x\n", egl.GetError());
        return -1;
    }
    egl.SwapInterval(g_dpy, 1);
    egl.QuerySurface(g_dpy, g_surf, EGL_WIDTH, &g_surf_w);
    egl.QuerySurface(g_dpy, g_surf, EGL_HEIGHT, &g_surf_h);
    if (g_surf_w <= 0 || g_surf_h <= 0) { g_surf_w = 1920; g_surf_h = 1080; }
    log_line("[uwp] EGL %d.%d surface %dx%d\n", major, minor, g_surf_w, g_surf_h);
    return 0;
}

extern "C" int uwp_surface_size(int *w, int *h)
{
    if (!g_surf) return -1;
    *w = g_surf_w;
    *h = g_surf_h;
    return 0;
}

extern "C" void uwp_swap(void)
{
    if (g_surf) egl.SwapBuffers(g_dpy, g_surf);
}

/* ---- events ------------------------------------------------------------ */
extern "C" void uwp_pump_events(void)
{
    if (g_dispatcher) g_dispatcher->ProcessEvents(WUC::CoreProcessEventsOption_ProcessAllIfPresent);
}

extern "C" int uwp_window_closed(void) { return g_closed; }

extern "C" int uwp_take_escape(void)
{
    int e = g_escape;
    g_escape = 0;
    return e;
}

extern "C" int uwp_pointer(int *x, int *y, int *down)
{
    if (!g_ptr_have) { *down = 0; return 0; }
    *x = g_ptr_x;
    *y = g_ptr_y;
    *down = g_ptr_down;
    return 1;
}

static void on_pointer(WUC::IPointerEventArgs *args)
{
    ComPtr<WUI::IPointerPoint> pt;
    if (FAILED(args->get_CurrentPoint(&pt))) return;
    WF::Point pos;
    pt->get_Position(&pos);
    ComPtr<WUI::IPointerPointProperties> pp;
    boolean left = false;
    if (SUCCEEDED(pt->get_Properties(&pp))) pp->get_IsLeftButtonPressed(&left);
    boolean contact = false;
    pt->get_IsInContact(&contact);
    WF::Rect b = { 0, 0, 0, 0 };
    if (g_window) g_window->get_Bounds(&b);
    if (b.Width <= 0 || b.Height <= 0) return;
    g_ptr_x = (int)(pos.X * g_surf_w / b.Width);
    g_ptr_y = (int)(pos.Y * g_surf_h / b.Height);
    g_ptr_down = (left || contact) ? 1 : 0;
    g_ptr_have = 1;
}

static void on_key(WUC::IKeyEventArgs *args, int down)
{
    ABI::Windows::System::VirtualKey vk;
    if (FAILED(args->get_VirtualKey(&vk))) return;
    int k = (int)vk;
    /* Controller buttons also arrive as keys on Xbox; they are read through
     * Windows.Gaming.Input instead. */
    if (k >= 195 && k <= 218) return;
    if (down) {
        WUC::CorePhysicalKeyStatus st;
        if (SUCCEEDED(args->get_KeyStatus(&st)) && st.WasKeyDown) return;   /* repeat */
        if (k == VK_ESCAPE) g_escape = 1;
    }
    platform_key_event(k, down);
}

/* ---- gamepad (Windows.Gaming.Input -> XINPUT_STATE) -------------------- */
static ComPtr<WGI::IGamepadStatics> g_pad_statics;
static ComPtr<WGI::IGamepad> g_pad;
static int g_pad_poll;

extern "C" DWORD WINAPI uwp_xinput_get_state(DWORD index, XINPUT_STATE *state)
{
    if (index != 0 || !state) return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_pad_statics &&
        FAILED(get_factory(RuntimeClass_Windows_Gaming_Input_Gamepad, g_pad_statics.GetAddressOf())))
        return ERROR_DEVICE_NOT_CONNECTED;
    /* The controller list fills in asynchronously and can change; refresh
     * it every second or so instead of every frame. */
    if (!g_pad || (++g_pad_poll % 60) == 0) {
        ComPtr<WFC::IVectorView<WGI::Gamepad *>> pads;
        unsigned n = 0;
        g_pad.Reset();
        if (SUCCEEDED(g_pad_statics->get_Gamepads(&pads)) && SUCCEEDED(pads->get_Size(&n)) && n > 0)
            pads->GetAt(0, &g_pad);
    }
    if (!g_pad) return ERROR_DEVICE_NOT_CONNECTED;
    WGI::GamepadReading r;
    if (FAILED(g_pad->GetCurrentReading(&r))) return ERROR_DEVICE_NOT_CONNECTED;

    static const struct { unsigned wgi; WORD xi; } map[] = {
        { WGI::GamepadButtons_Menu, XINPUT_GAMEPAD_START },
        { WGI::GamepadButtons_View, XINPUT_GAMEPAD_BACK },
        { WGI::GamepadButtons_A, XINPUT_GAMEPAD_A },
        { WGI::GamepadButtons_B, XINPUT_GAMEPAD_B },
        { WGI::GamepadButtons_X, XINPUT_GAMEPAD_X },
        { WGI::GamepadButtons_Y, XINPUT_GAMEPAD_Y },
        { WGI::GamepadButtons_DPadUp, XINPUT_GAMEPAD_DPAD_UP },
        { WGI::GamepadButtons_DPadDown, XINPUT_GAMEPAD_DPAD_DOWN },
        { WGI::GamepadButtons_DPadLeft, XINPUT_GAMEPAD_DPAD_LEFT },
        { WGI::GamepadButtons_DPadRight, XINPUT_GAMEPAD_DPAD_RIGHT },
        { WGI::GamepadButtons_LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER },
        { WGI::GamepadButtons_RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { WGI::GamepadButtons_LeftThumbstick, XINPUT_GAMEPAD_LEFT_THUMB },
        { WGI::GamepadButtons_RightThumbstick, XINPUT_GAMEPAD_RIGHT_THUMB },
    };
    memset(state, 0, sizeof(*state));
    for (const auto &m : map)
        if ((unsigned)r.Buttons & m.wgi) state->Gamepad.wButtons |= m.xi;
    auto axis = [](double v) -> SHORT {
        if (v < -1.0) v = -1.0;
        if (v > 1.0) v = 1.0;
        return (SHORT)(v * 32767.0);
    };
    auto trig = [](double v) -> BYTE {
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        return (BYTE)(v * 255.0);
    };
    state->Gamepad.sThumbLX = axis(r.LeftThumbstickX);
    state->Gamepad.sThumbLY = axis(r.LeftThumbstickY);
    state->Gamepad.sThumbRX = axis(r.RightThumbstickX);
    state->Gamepad.sThumbRY = axis(r.RightThumbstickY);
    state->Gamepad.bLeftTrigger = trig(r.LeftTrigger);
    state->Gamepad.bRightTrigger = trig(r.RightTrigger);
    state->dwPacketNumber = (DWORD)r.Timestamp;
    return ERROR_SUCCESS;
}

/* ---- status screens (no font engine: 5x7 glyphs via scissored clears) -- */
static const unsigned char kFont[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
};

static struct {
    void (APIENTRY *Enable)(unsigned);
    void (APIENTRY *Disable)(unsigned);
    void (APIENTRY *Scissor)(int, int, int, int);
    void (APIENTRY *Viewport)(int, int, int, int);
    void (APIENTRY *ClearColor)(float, float, float, float);
    void (APIENTRY *Clear)(unsigned);
    void (APIENTRY *ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
} gl;

static int screen_gl(void)
{
    if (gl.Clear) return 1;
    if (!g_surf) return 0;
    *(void **)&gl.Enable = uwp_gl_getproc("glEnable");
    *(void **)&gl.Disable = uwp_gl_getproc("glDisable");
    *(void **)&gl.Scissor = uwp_gl_getproc("glScissor");
    *(void **)&gl.Viewport = uwp_gl_getproc("glViewport");
    *(void **)&gl.ClearColor = uwp_gl_getproc("glClearColor");
    *(void **)&gl.Clear = uwp_gl_getproc("glClear");
    *(void **)&gl.ColorMask = uwp_gl_getproc("glColorMask");
    return gl.Clear && gl.Scissor && gl.Enable && gl.ColorMask;
}

static void rect(int x, int y, int w, int h, float r, float g, float b)
{
    if (w <= 0 || h <= 0) return;
    gl.Scissor(x, g_surf_h - y - h, w, h);
    gl.ClearColor(r, g, b, 1.0f);
    gl.Clear(0x4000);
}

static int text(int x, int y, int s, const char *str, float r, float g, float b)
{
    for (const char *p = str; *p; p++, x += 6 * s) {
        int c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = '?';
        const unsigned char *gly = kFont[c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (gly[col] & (1 << row)) rect(x + col * s, y + row * s, s, s, r, g, b);
    }
    return x;
}

static void screen_begin(void)
{
    gl.Viewport(0, 0, g_surf_w, g_surf_h);
    gl.ColorMask(1, 1, 1, 1);
    gl.Disable(0x0C11);
    gl.ClearColor(0.02f, 0.03f, 0.08f, 1.0f);
    gl.Clear(0x4000 | 0x0100);
    gl.Enable(0x0C11);
}

static void screen_end(void)
{
    gl.Disable(0x0C11);
    uwp_swap();
}

extern "C" void uwp_progress(float frac)
{
    if (!screen_gl()) return;
    screen_begin();
    int s = g_surf_h / 160 + 1;
    text(g_surf_w / 10, g_surf_h / 3, s + 2, "ASPHALT 5", 1.0f, 0.75f, 0.1f);
    if (frac >= 0.0f) {
        text(g_surf_w / 10, g_surf_h / 3 + 14 * (s + 2), s,
             "SETTING UP GAME FILES (FIRST LAUNCH ONLY)...", 0.9f, 0.9f, 0.9f);
        int bx = g_surf_w / 10, by = g_surf_h / 2 + 20, bw = g_surf_w * 8 / 10, bh = s * 6;
        rect(bx, by, bw, bh, 0.25f, 0.25f, 0.3f);
        rect(bx, by, (int)(bw * (frac > 1.0f ? 1.0f : frac)), bh, 1.0f, 0.75f, 0.1f);
    }
    screen_end();
    uwp_pump_events();
}

extern "C" void uwp_message_box(const char *msg, const char *title)
{
    log_line("[uwp] %s: %s\n", title ? title : "", msg ? msg : "");
    if (!screen_gl()) return;
    for (;;) {
        screen_begin();
        int s = g_surf_h / 200 + 1;
        int x0 = g_surf_w / 12, y = g_surf_h / 8;
        text(x0, y, s + 2, title ? title : "Asphalt 5", 1.0f, 0.35f, 0.25f);
        y += 16 * (s + 2);
        /* word-wrap at the screen width */
        int cols = (g_surf_w - 2 * x0) / (6 * s);
        const char *p = msg ? msg : "";
        char line[256];
        while (*p) {
            int n = 0, brk = -1;
            while (p[n] && p[n] != '\n' && n < cols && n < (int)sizeof(line) - 1) {
                if (p[n] == ' ') brk = n;
                n++;
            }
            if (p[n] && p[n] != '\n' && brk > 0) n = brk;
            memcpy(line, p, (size_t)n);
            line[n] = 0;
            text(x0, y, s, line, 0.92f, 0.92f, 0.92f);
            y += 10 * s;
            p += n;
            if (*p == '\n' || *p == ' ') p++;
        }
        text(x0, g_surf_h - 20 * s, s, "Press the Xbox button to leave.", 0.6f, 0.6f, 0.7f);
        screen_end();
        if (g_dispatcher)
            g_dispatcher->ProcessEvents(WUC::CoreProcessEventsOption_ProcessOneAndAllPending);
        if (g_closed) return;
    }
}

/* ---- paths ------------------------------------------------------------- */
static int storage_path(ComPtr<WS::IStorageFolder> folder, wchar_t *out)
{
    ComPtr<WS::IStorageItem> item;
    HSTRING path = nullptr;
    if (!folder || FAILED(folder.As(&item)) || FAILED(item->get_Path(&path))) return -1;
    UINT32 len = 0;
    const wchar_t *raw = WindowsGetStringRawBuffer(path, &len);
    wcsncpy(out, raw ? raw : L"", MAX_PATH - 1);
    out[MAX_PATH - 1] = 0;
    WindowsDeleteString(path);
    return 0;
}

static void init_paths(void)
{
    ComPtr<WS::IApplicationDataStatics> ads;
    ComPtr<WS::IApplicationData> ad;
    ComPtr<WS::IStorageFolder> local;
    if (SUCCEEDED(get_factory(RuntimeClass_Windows_Storage_ApplicationData, ads.GetAddressOf())) &&
        SUCCEEDED(ads->get_Current(&ad)) && SUCCEEDED(ad->get_LocalFolder(&local)))
        storage_path(local, g_local);

    ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> ps;
    ComPtr<ABI::Windows::ApplicationModel::IPackage> pkg;
    ComPtr<WS::IStorageFolder> inst;
    if (SUCCEEDED(get_factory(RuntimeClass_Windows_ApplicationModel_Package, ps.GetAddressOf())) &&
        SUCCEEDED(ps->get_Current(&pkg)) && SUCCEEDED(pkg->get_InstalledLocation(&inst)))
        storage_path(inst, g_install);

    /* LocalState is the only writable folder: it becomes the working
     * directory (saves, data, logs) and LOCALAPPDATA for the shared code. */
    SetCurrentDirectoryW(g_local);
    wchar_t env[MAX_PATH + 16];
    _snwprintf(env, MAX_PATH + 15, L"LOCALAPPDATA=%ls", g_local);
    env[MAX_PATH + 15] = 0;
    _wputenv(env);
    wchar_t logdir[MAX_PATH + 16], logpath[MAX_PATH + 32];
    _snwprintf(logdir, MAX_PATH + 15, L"%ls\\Asphalt5", g_local);
    logdir[MAX_PATH + 15] = 0;
    CreateDirectoryW(logdir, nullptr);
    _snwprintf(logpath, MAX_PATH + 31, L"%ls\\asphalt5.log", logdir);
    logpath[MAX_PATH + 31] = 0;
    _wfreopen(logpath, L"w", stderr);
    _wfreopen(logpath, L"a", stdout);
    log_line("[uwp] local %ls\n[uwp] install %ls\n", g_local, g_install);
}

/* ---- the game ---------------------------------------------------------- */
static void run_game(void)
{
    init_paths();
    uwp_pump_events();
    if (egl_init() != 0) {
        log_line("[uwp] graphics init failed\n");
        return;
    }
    uwp_progress(-1.0f);
    const char *err = uwp_import_game_files(g_local, g_install);
    if (err) {
        uwp_message_box(err, "Asphalt 5 - game files needed");
        return;
    }
    static char arg0[] = "Asphalt5";
    char *argv[] = { arg0, nullptr };
    int rc = recomp_main(1, argv);
    log_line("[uwp] game returned %d\n", rc);
}

/* ---- IFrameworkView ---------------------------------------------------- */
typedef WF::ITypedEventHandler<WAC::CoreApplicationView *, WAA::IActivatedEventArgs *> ActivatedHandler;
typedef WF::ITypedEventHandler<WUC::CoreWindow *, WUC::KeyEventArgs *> KeyHandler;
typedef WF::ITypedEventHandler<WUC::CoreWindow *, WUC::PointerEventArgs *> PointerHandler;
typedef WF::ITypedEventHandler<WUC::CoreWindow *, WUC::CoreWindowEventArgs *> ClosedHandler;
typedef WF::ITypedEventHandler<WUC::CoreWindow *, WUC::WindowActivatedEventArgs *> FocusHandler;

struct View : WAC::IFrameworkView {
    LONG ref = 1;
    virtual ~View() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **pp) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) ||
            iid == __uuidof(WAC::IFrameworkView)) {
            *pp = static_cast<WAC::IFrameworkView *>(this);
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **ids) override { *n = 0; *ids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override { *name = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE Initialize(WAC::ICoreApplicationView *view) override
    {
        EventRegistrationToken tok;
        auto *h = new Handler<ActivatedHandler, WAC::ICoreApplicationView *, WAA::IActivatedEventArgs *>(
            [](WAC::ICoreApplicationView *, WAA::IActivatedEventArgs *) -> HRESULT {
                if (g_window) g_window->Activate();
                return S_OK;
            });
        view->add_Activated(h, &tok);
        h->Release();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetWindow(WUC::ICoreWindow *window) override
    {
        g_window = window;
        window->get_Dispatcher(&g_dispatcher);
        EventRegistrationToken tok;
        auto key = [](int down) {
            return new Handler<KeyHandler, WUC::ICoreWindow *, WUC::IKeyEventArgs *>(
                [down](WUC::ICoreWindow *, WUC::IKeyEventArgs *a) -> HRESULT {
                    on_key(a, down);
                    return S_OK;
                });
        };
        auto *kd = key(1), *ku = key(0);
        window->add_KeyDown(kd, &tok);
        window->add_KeyUp(ku, &tok);
        kd->Release();
        ku->Release();
        auto *ptr = new Handler<PointerHandler, WUC::ICoreWindow *, WUC::IPointerEventArgs *>(
            [](WUC::ICoreWindow *, WUC::IPointerEventArgs *a) -> HRESULT {
                on_pointer(a);
                return S_OK;
            });
        window->add_PointerPressed(ptr, &tok);
        window->add_PointerReleased(ptr, &tok);
        window->add_PointerMoved(ptr, &tok);
        ptr->Release();
        auto *closed = new Handler<ClosedHandler, WUC::ICoreWindow *, WUC::ICoreWindowEventArgs *>(
            [](WUC::ICoreWindow *, WUC::ICoreWindowEventArgs *) -> HRESULT {
                g_closed = 1;
                return S_OK;
            });
        window->add_Closed(closed, &tok);
        closed->Release();
        auto *focus = new Handler<FocusHandler, WUC::ICoreWindow *, WUC::IWindowActivatedEventArgs *>(
            [](WUC::ICoreWindow *, WUC::IWindowActivatedEventArgs *a) -> HRESULT {
                WUC::CoreWindowActivationState st;
                if (SUCCEEDED(a->get_WindowActivationState(&st)) &&
                    st == WUC::CoreWindowActivationState_Deactivated)
                    platform_focus_lost();
                return S_OK;
            });
        window->add_Activated(focus, &tok);
        focus->Release();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Run() override
    {
        if (g_window) g_window->Activate();
        run_game();
        ComPtr<WAC::ICoreApplicationExit> app_exit;
        if (SUCCEEDED(get_factory(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication,
                                  app_exit.GetAddressOf())))
            app_exit->Exit();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Uninitialize() override { return S_OK; }
};

struct ViewSource : WAC::IFrameworkViewSource {
    LONG ref = 1;
    virtual ~ViewSource() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **pp) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) ||
            iid == __uuidof(WAC::IFrameworkViewSource)) {
            *pp = static_cast<WAC::IFrameworkViewSource *>(this);
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **ids) override { *n = 0; *ids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override { *name = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateView(WAC::IFrameworkView **view) override
    {
        *view = new View();
        return S_OK;
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 1;
    ComPtr<WAC::ICoreApplication> app;
    if (FAILED(get_factory(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication, app.GetAddressOf())))
        return 1;
    ViewSource *src = new ViewSource();
    app->Run(src);
    src->Release();
    return 0;
}
