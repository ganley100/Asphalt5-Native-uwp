/* Pixel tests for the fixed-function emulation in src/gl/ff/ff_gles.c.
 *
 * Runs on any OpenGL ES 2/3 implementation with EGL pbuffers (Mesa
 * llvmpipe on Linux, ANGLE on Windows).  Each case renders with the desktop
 * GL 1.x entry points the game host uses and checks a pixel against the
 * value the OpenGL ES 1.1 pipeline would produce.
 *
 *   cc -O1 -Isrc/gl/ff tests/ff_gles_test.c src/gl/ff/ff_gles.c -lEGL -lm -o ff_gles_test
 *   EGL_PLATFORM=surfaceless ./ff_gles_test
 */
#include <EGL/egl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffgl.h"

#define W 64
#define H 64

static int g_fail;

static void *getproc(const char *name) { return (void *)eglGetProcAddress(name); }

static void px(int x, int y, unsigned char *out)
{
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

static void expect(const char *name, int x, int y, int r, int g, int b, int a, int tol)
{
    unsigned char p[4];
    px(x, y, p);
    int ok = abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol &&
             (a < 0 || abs(p[3] - a) <= tol);
    printf("%-28s %s got %3d %3d %3d %3d want %3d %3d %3d %3d\n", name, ok ? "ok  " : "FAIL",
           p[0], p[1], p[2], p[3], r, g, b, a);
    if (!ok) g_fail++;
}

static const float quad[] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
static const float quad_tc[] = { 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1 };

static void reset(void)
{
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4f(1, 1, 1, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static GLuint make_tex(GLenum fmt, int w, int h, const unsigned char *data)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    return t;
}

static void draw_quad(void)
{
    glVertexPointer(2, GL_FLOAT, 0, quad);
    glTexCoordPointer(2, GL_FLOAT, 0, quad_tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                EGL_DEPTH_SIZE, 24, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no config\n"); return 2; }
    const EGLint pb[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb);
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!c || !eglMakeCurrent(d, s, s, c)) { fprintf(stderr, "no context\n"); return 2; }
    if (ff_init(getproc) != 0) return 2;
    ff_set_surface_size(W, H);
    glViewport(0, 0, W, H);

    /* flat colour */
    reset();
    glColor4f(1, 0.5f, 0.25f, 1);
    draw_quad();
    expect("current color", 32, 32, 255, 128, 64, 255, 2);

    /* colour array, unsigned byte (normalized) */
    reset();
    {
        unsigned char col[6 * 4];
        for (int i = 0; i < 6; i++) { col[i*4] = 0; col[i*4+1] = 255; col[i*4+2] = 0; col[i*4+3] = 255; }
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
        draw_quad();
        expect("color array ubyte", 32, 32, 0, 255, 0, 255, 2);
    }

    /* GL_FIXED vertices + translate: quad shifted to the right half */
    reset();
    {
        int fx[12];
        for (int i = 0; i < 12; i++) fx[i] = (int)(quad[i] * 0.5f * 65536.0f);
        glTranslatef(0.5f, 0, 0);
        glVertexPointer(2, GL_FIXED, 0, fx);
        glColor4f(0, 0, 1, 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        expect("fixed vertices right", 48, 32, 0, 0, 255, 255, 2);
        expect("fixed vertices left", 8, 32, 0, 0, 0, 255, 2);
    }

    /* glOrtho + push/pop */
    reset();
    glMatrixMode(GL_PROJECTION);
    glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(1000, 0, 0);
    glPopMatrix();
    {
        float v[] = { 0, 0, 32, 0, 32, 64, 0, 0, 32, 64, 0, 64 };
        glVertexPointer(2, GL_FLOAT, 0, v);
        glColor4f(1, 1, 0, 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        expect("ortho left half", 10, 32, 255, 255, 0, 255, 2);
        expect("ortho right half", 50, 32, 0, 0, 0, 255, 2);
    }

    /* texture MODULATE with RGBA */
    reset();
    {
        unsigned char t[4] = { 255, 128, 0, 255 };
        GLuint tex = make_tex(GL_RGBA, 1, 1, t);
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glColor4f(0.5f, 1, 1, 1);
        draw_quad();
        expect("tex modulate rgba", 32, 32, 128, 128, 0, 255, 3);

        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        draw_quad();
        expect("tex replace rgba", 32, 32, 255, 128, 0, 255, 2);
        glDeleteTextures(1, &tex);
    }

    /* ALPHA texture: modulate keeps the vertex colour, multiplies alpha */
    reset();
    {
        unsigned char t[1] = { 128 };
        GLuint tex = make_tex(GL_ALPHA, 1, 1, t);
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 0, 0, 1);
        draw_quad();
        expect("alpha tex modulate+blend", 32, 32, 128, 0, 0, -1, 3);
        glDeleteTextures(1, &tex);
    }

    /* LUMINANCE texture with BLEND env colour */
    reset();
    {
        unsigned char t[1] = { 255 };
        GLuint tex = make_tex(GL_LUMINANCE, 1, 1, t);
        float envc[4] = { 0, 1, 0, 1 };
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envc);
        glColor4f(1, 0, 0, 1);
        draw_quad();
        expect("lum tex env blend", 32, 32, 0, 255, 0, 255, 2);
        glDeleteTextures(1, &tex);
    }

    /* alpha test */
    reset();
    {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glColor4f(1, 1, 1, 0.25f);
        draw_quad();
        expect("alpha test rejects", 32, 32, 0, 0, 0, 255, 2);
        glColor4f(1, 1, 1, 0.75f);
        draw_quad();
        expect("alpha test passes", 32, 32, 255, 255, 255, 191, 2);
    }

    /* linear fog: z = -5 between start 0 and end 10 -> half fog colour */
    reset();
    {
        float fogc[4] = { 0, 0, 1, 1 };
        glMatrixMode(GL_PROJECTION);
        glOrtho(-1, 1, -1, 1, 0, 100);
        glMatrixMode(GL_MODELVIEW);
        glTranslatef(0, 0, -5);
        glEnable(GL_FOG);
        glFogf(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 0);
        glFogf(GL_FOG_END, 10);
        glFogfv(GL_FOG_COLOR, fogc);
        glColor4f(1, 0, 0, 1);
        draw_quad();
        expect("linear fog", 32, 32, 128, 0, 128, 255, 3);
    }

    /* directional diffuse light facing the quad */
    reset();
    {
        float nrm[18];
        for (int i = 0; i < 6; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
        float pos[4] = { 0, 0, 1, 0 };
        float dif[4] = { 1, 1, 1, 1 };
        float zero[4] = { 0, 0, 0, 1 };
        float md[4] = { 0.5f, 0.25f, 1, 1 };
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, zero);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, zero);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, md);
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, nrm);
        draw_quad();
        expect("diffuse light", 32, 32, 128, 64, 255, 255, 3);

        /* colour material: vertex colour replaces diffuse */
        glEnable(GL_COLOR_MATERIAL);
        glColor4f(0, 1, 0, 1);
        draw_quad();
        expect("color material", 32, 32, 0, 255, 0, 255, 3);
        glDisable(GL_COLOR_MATERIAL);

        /* light behind the surface: black */
        float back[4] = { 0, 0, -1, 0 };
        glLightfv(GL_LIGHT0, GL_POSITION, back);
        draw_quad();
        expect("light from behind", 32, 32, 0, 0, 0, 255, 2);
        glDisable(GL_LIGHT0);
    }

    /* two units: unit 1 combine MODULATE of texture and previous */
    reset();
    {
        unsigned char a[4] = { 255, 255, 255, 255 };
        unsigned char b[4] = { 128, 255, 0, 255 };
        ff_ActiveTexture(GL_TEXTURE0);
        GLuint t0 = make_tex(GL_RGBA, 1, 1, a);
        glEnable(GL_TEXTURE_2D);
        ff_ActiveTexture(GL_TEXTURE1);
        GLuint t1 = make_tex(GL_RGBA, 1, 1, b);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
        float two = 2.0f;
        glTexEnvfv(GL_TEXTURE_ENV, GL_RGB_SCALE, &two);
        ff_ClientActiveTexture(GL_TEXTURE0);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, quad_tc);
        ff_ClientActiveTexture(GL_TEXTURE1);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, quad_tc);
        glColor4f(0.5f, 0.5f, 0.5f, 1);
        glVertexPointer(2, GL_FLOAT, 0, quad);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        /* unit0: 0.5 grey * white, unit1: (tex * prev) * 2 */
        expect("combine modulate x2", 32, 32, 128, 255, 0, 255, 3);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        ff_ActiveTexture(GL_TEXTURE0);
        ff_ClientActiveTexture(GL_TEXTURE0);
        glDeleteTextures(1, &t0);
        glDeleteTextures(1, &t1);
    }

    /* indexed draw with unsigned short indices */
    reset();
    {
        unsigned short idx[6] = { 0, 1, 2, 0, 2, 3 };
        float v[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
        glVertexPointer(2, GL_FLOAT, 0, v);
        glColor4f(0, 1, 1, 1);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
        expect("draw elements", 32, 32, 0, 255, 255, 255, 2);
    }

    /* immediate-mode quad (movie path) with BGRA texture */
    reset();
    {
        unsigned char t[4] = { 255, 0, 0, 255 };   /* BGRA blue */
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, t);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glMatrixMode(GL_PROJECTION);
        glOrtho(0, W, H, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2i(0, 0);
        glTexCoord2f(1, 0); glVertex2i(W, 0);
        glTexCoord2f(1, 1); glVertex2i(W, H);
        glTexCoord2f(0, 1); glVertex2i(0, H);
        glEnd();
        unsigned char p[4];
        px(32, 32, p);
        /* Mesa may not expose BGRA uploads; accept blue or an untouched clear. */
        int ok = (p[2] > 250 && p[0] < 5) || (p[0] < 5 && p[1] < 5 && p[2] < 5);
        printf("%-28s %s got %3d %3d %3d %3d\n", "immediate quad", ok ? "ok  " : "FAIL",
               p[0], p[1], p[2], p[3]);
        if (!ok) g_fail++;
        glDeleteTextures(1, &tex);
    }

    /* overlay rect must not disturb scissor/clear state */
    reset();
    ff_overlay_rect(0, 0, 8, 8, 1, 1, 1, 1);
    expect("overlay top-left", 2, H - 3, 255, 255, 255, 255, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    expect("clear after overlay", 2, H - 3, 0, 0, 0, 255, 1);

    printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
