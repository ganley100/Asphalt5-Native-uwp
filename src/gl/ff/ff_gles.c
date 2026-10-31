/* Fixed-function OpenGL 1.x on OpenGL ES 2/3 (ANGLE / Direct3D 11).
 *
 * The UWP build has no opengl32.dll.  This file implements the desktop GL
 * subset declared in ffgl.h on top of OpenGL ES: server state that ES still
 * has (blend, depth, cull, scissor, viewport, textures) is forwarded, and
 * everything ES dropped (matrix stacks, client array semantics, lighting,
 * fog, alpha test, texture environments) is tracked here and turned into
 * generated GLSL ES 1.00 programs, cached by state key.
 *
 * Semantics follow the OpenGL ES 1.1 specification, which is what the game
 * was written against: per-vertex lighting with an infinite viewer, eye-z
 * fog, and the texture environment tables of section 3.7.12.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "ffgl.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FF_UNITS 4
#define FF_LIGHTS 8
#define FF_STACK 32
#define FF_PROGRAMS 256

/* ---- OpenGL ES entry points ------------------------------------------- */
#define GLES_VERTEX_SHADER 0x8B31
#define GLES_FRAGMENT_SHADER 0x8B30
#define GLES_COMPILE_STATUS 0x8B81
#define GLES_LINK_STATUS 0x8B82
#define GLES_INFO_LOG_LENGTH 0x8B84

#define ES_FUNCS(X) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, ClearDepthf, (GLfloat)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, CompileShader, (GLuint)) \
    X(void, CompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)) \
    X(void, CopyTexImage2D, (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) \
    X(GLuint, CreateProgram, (void)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, CullFace, (GLenum)) \
    X(void, DeleteShader, (GLuint)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, Disable, (GLenum)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
    X(void, Enable, (GLenum)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, Finish, (void)) \
    X(void, FrontFace, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, GetBooleanv, (GLenum, GLboolean *)) \
    X(GLenum, GetError, (void)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Hint, (GLenum, GLenum)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void, UseProgram, (GLuint)) \
    X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define X(ret, name, args) ret (APIENTRY *name) args;
static struct { ES_FUNCS(X) } es;
#undef X

/* ---- emulated state ---------------------------------------------------- */
enum { FMT_RGBA, FMT_RGB, FMT_ALPHA, FMT_LUM, FMT_LUM_ALPHA };
enum { ENV_MODULATE, ENV_REPLACE, ENV_DECAL, ENV_BLEND, ENV_ADD, ENV_COMBINE };

typedef struct {
    GLint size; GLenum type; GLsizei stride; const void *ptr; int on;
} ClientArray;

typedef struct {
    int mode;                   /* ENV_* */
    GLfloat color[4];
    GLenum comb_rgb, comb_alpha;
    GLenum src_rgb[3], src_a[3], op_rgb[3], op_a[3];
    GLfloat rgb_scale, a_scale;
} TexEnv;

typedef struct {
    GLfloat amb[4], dif[4], spec[4], pos[4];   /* pos in eye space */
    GLfloat spot_dir[3], spot_exp, spot_cut;
    GLfloat att[3];
    int on;
} Light;

typedef struct { int w, h, fmt, genmip, valid; } TexInfo;

typedef struct {
    float m[FF_STACK][16];
    int top;
} MatStack;

static struct {
    MatStack mv, proj, tex[FF_UNITS];
    GLenum mode;
    int active, client;
    GLuint bound[FF_UNITS];
    int tex2d[FF_UNITS];
    TexEnv env[FF_UNITS];
    ClientArray vtx, nrm, col, tc[FF_UNITS];
    GLfloat color[4], normal[3], texcoord[4];
    int lighting, color_material, normalize, fog, alpha_test;
    int misc_caps[8];           /* ignored desktop/GLES1 caps, for glIsEnabled */
    GLenum misc_cap_id[8];
    Light light[FF_LIGHTS];
    GLfloat mat_amb[4], mat_dif[4], mat_spec[4], mat_emi[4], mat_shin;
    GLfloat lm_amb[4];
    GLenum fog_mode;
    GLfloat fog_density, fog_start, fog_end, fog_color[4];
    GLenum alpha_func;
    GLfloat alpha_ref;
    GLenum shade_model;
    int surf_w, surf_h;
    unsigned attr_on;           /* enabled ES vertex attribute arrays */
    GLuint cur_prog;
} S;

/* Uniform groups re-uploaded when their serial changes. */
enum { G_MTX, G_LIGHT, G_MAT, G_FOG, G_ENV, G_AREF, G_COUNT };
static unsigned g_serial[G_COUNT] = { 1, 1, 1, 1, 1, 1 };
#define DIRTY(g) (g_serial[(g)]++)

static TexInfo *g_tex;
static unsigned g_tex_cap;

static TexInfo *tex_info(GLuint name)
{
    if (name >= (1u << 20)) return NULL;
    if (name >= g_tex_cap) {
        unsigned cap = g_tex_cap ? g_tex_cap : 1024;
        while (cap <= name) cap *= 2;
        TexInfo *n = (TexInfo *)realloc(g_tex, cap * sizeof(TexInfo));
        if (!n) return NULL;
        memset(n + g_tex_cap, 0, (cap - g_tex_cap) * sizeof(TexInfo));
        g_tex = n; g_tex_cap = cap;
    }
    return &g_tex[name];
}

/* ---- matrices ---------------------------------------------------------- */
static void m_ident(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void m_mul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] +
                            a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
    memcpy(out, r, sizeof(r));
}

static MatStack *cur_stack(void)
{
    if (S.mode == GL_PROJECTION) return &S.proj;
    if (S.mode == GL_TEXTURE) return &S.tex[S.active];
    return &S.mv;
}

static float *cur_mat(void)
{
    MatStack *s = cur_stack();
    return s->m[s->top];
}

static void mult_cur(const float *b)
{
    float *m = cur_mat();
    m_mul(m, m, b);
    DIRTY(G_MTX);
}

/* ---- init -------------------------------------------------------------- */
static void state_defaults(void)
{
    memset(&S, 0, sizeof(S));
    m_ident(S.mv.m[0]);
    m_ident(S.proj.m[0]);
    for (int u = 0; u < FF_UNITS; u++) {
        m_ident(S.tex[u].m[0]);
        TexEnv *e = &S.env[u];
        e->mode = ENV_MODULATE;
        e->comb_rgb = e->comb_alpha = GL_MODULATE;
        e->src_rgb[0] = e->src_a[0] = GL_TEXTURE;
        e->src_rgb[1] = e->src_a[1] = GL_PREVIOUS;
        e->src_rgb[2] = e->src_a[2] = GL_CONSTANT;
        e->op_rgb[0] = e->op_rgb[1] = GL_SRC_COLOR;
        e->op_rgb[2] = GL_SRC_ALPHA;
        e->op_a[0] = e->op_a[1] = e->op_a[2] = GL_SRC_ALPHA;
        e->rgb_scale = e->a_scale = 1.0f;
    }
    S.mode = GL_MODELVIEW;
    S.color[0] = S.color[1] = S.color[2] = S.color[3] = 1.0f;
    S.normal[2] = 1.0f;
    S.texcoord[3] = 1.0f;
    for (int i = 0; i < FF_LIGHTS; i++) {
        Light *l = &S.light[i];
        l->amb[3] = 1.0f;
        if (i == 0) {
            l->dif[0] = l->dif[1] = l->dif[2] = l->dif[3] = 1.0f;
            l->spec[0] = l->spec[1] = l->spec[2] = l->spec[3] = 1.0f;
        } else {
            l->dif[3] = l->spec[3] = 1.0f;
        }
        l->pos[2] = 1.0f;
        l->spot_dir[2] = -1.0f;
        l->spot_cut = 180.0f;
        l->att[0] = 1.0f;
    }
    S.mat_amb[0] = S.mat_amb[1] = S.mat_amb[2] = 0.2f; S.mat_amb[3] = 1.0f;
    S.mat_dif[0] = S.mat_dif[1] = S.mat_dif[2] = 0.8f; S.mat_dif[3] = 1.0f;
    S.mat_spec[3] = 1.0f;
    S.mat_emi[3] = 1.0f;
    S.lm_amb[0] = S.lm_amb[1] = S.lm_amb[2] = 0.2f; S.lm_amb[3] = 1.0f;
    S.fog_mode = GL_EXP;
    S.fog_density = 1.0f;
    S.fog_end = 1.0f;
    S.alpha_func = GL_ALWAYS;
    S.shade_model = GL_SMOOTH;
}

int ff_init(void *(*getproc)(const char *name))
{
    int missing = 0;
#define X(ret, name, args) \
    es.name = (ret (APIENTRY *) args)getproc("gl" #name); \
    if (!es.name) { fprintf(stderr, "[ff] missing gl%s\n", #name); missing++; }
    ES_FUNCS(X)
#undef X
    if (missing) return -1;
    state_defaults();
    tex_info(0);
    fprintf(stderr, "[ff] GLES %s / %s\n", (const char *)es.GetString(GL_VERSION),
            (const char *)es.GetString(GL_RENDERER));
    return 0;
}

void ff_set_surface_size(int w, int h) { S.surf_w = w; S.surf_h = h; }

/* ---- program generation ------------------------------------------------ */
typedef struct {
    uint8_t tex_on;
    uint8_t env[FF_UNITS];
    uint8_t fmt[FF_UNITS];
    uint8_t comb[FF_UNITS][16];
    uint8_t lights;             /* enabled light mask when lighting is on */
    uint8_t lighting;
    uint8_t color_material;
    uint8_t fog;                /* 0 off, 1 linear, 2 exp, 3 exp2 */
    uint8_t alpha;              /* 0 off, else 1 + (func - GL_NEVER) */
    uint8_t pad[3];
} Key;

typedef struct {
    Key key;
    uint32_t hash;
    GLuint prog;
    GLint u_mv, u_proj, u_nmat, u_tm[FF_UNITS], u_envc[FF_UNITS];
    GLint u_fogc, u_fog, u_aref;
    GLint u_mamb, u_mdif, u_mspec, u_memi, u_mshin, u_lmamb;
    GLint u_lamb, u_ldif, u_lspec, u_lpos, u_lspot, u_latt;
    unsigned seen[G_COUNT];
} Prog;

static Prog g_progs[FF_PROGRAMS];
static int g_nprogs;

/* combine encoding: sources and operands as small indices */
static uint8_t enc_src(GLenum s)
{
    switch (s) {
    case GL_TEXTURE: return 0;
    case GL_CONSTANT: return 1;
    case GL_PRIMARY_COLOR: return 2;
    case GL_PREVIOUS: return 3;
    default:
        if (s >= GL_TEXTURE0 && s < GL_TEXTURE0 + FF_UNITS) return (uint8_t)(4 + (s - GL_TEXTURE0));
        return 3;
    }
}

static uint8_t enc_op(GLenum o)
{
    switch (o) {
    case GL_SRC_COLOR: return 0;
    case GL_ONE_MINUS_SRC_COLOR: return 1;
    case GL_SRC_ALPHA: return 2;
    default: return 3;  /* GL_ONE_MINUS_SRC_ALPHA */
    }
}

static uint8_t enc_func(GLenum f)
{
    switch (f) {
    case GL_REPLACE: return 0;
    case GL_MODULATE: return 1;
    case GL_ADD: return 2;
    case GL_ADD_SIGNED: return 3;
    case GL_INTERPOLATE: return 4;
    case GL_SUBTRACT: return 5;
    case GL_DOT3_RGB: return 6;
    case GL_DOT3_RGBA: return 7;
    default: return 1;
    }
}

static uint8_t enc_scale(GLfloat s) { return s >= 3.0f ? 2 : s >= 1.5f ? 1 : 0; }

static void build_key(Key *k)
{
    memset(k, 0, sizeof(*k));
    for (int u = 0; u < FF_UNITS; u++) {
        if (!S.tex2d[u]) continue;
        k->tex_on |= (uint8_t)(1u << u);
        TexEnv *e = &S.env[u];
        k->env[u] = (uint8_t)e->mode;
        TexInfo *ti = tex_info(S.bound[u]);
        k->fmt[u] = (uint8_t)(ti ? ti->fmt : FMT_RGBA);
        if (e->mode == ENV_COMBINE) {
            uint8_t *c = k->comb[u];
            c[0] = enc_func(e->comb_rgb);
            c[1] = enc_func(e->comb_alpha);
            for (int i = 0; i < 3; i++) {
                c[2 + i] = enc_src(e->src_rgb[i]);
                c[5 + i] = enc_op(e->op_rgb[i]);
                c[8 + i] = enc_src(e->src_a[i]);
                c[11 + i] = enc_op(e->op_a[i]);
            }
            c[14] = enc_scale(e->rgb_scale);
            c[15] = enc_scale(e->a_scale);
        }
    }
    if (S.lighting) {
        k->lighting = 1;
        for (int i = 0; i < FF_LIGHTS; i++)
            if (S.light[i].on) k->lights |= (uint8_t)(1u << i);
        k->color_material = (uint8_t)(S.color_material != 0);
    }
    if (S.fog)
        k->fog = S.fog_mode == GL_LINEAR ? 1 : S.fog_mode == GL_EXP2 ? 3 : 2;
    if (S.alpha_test && S.alpha_func != GL_ALWAYS)
        k->alpha = (uint8_t)(1 + (S.alpha_func - GL_NEVER));
}

static uint32_t key_hash(const Key *k)
{
    const uint8_t *p = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*k); i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

typedef struct { char *buf; size_t len, cap; } Str;

static void sp(Str *s, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (s->len + (size_t)n < s->cap) { s->len += (size_t)n; return; }
        size_t cap = s->cap ? s->cap * 2 : 4096;
        while (cap <= s->len + (size_t)n) cap *= 2;
        char *nb = (char *)realloc(s->buf, cap);
        if (!nb) return;
        s->buf = nb; s->cap = cap;
    }
}

static void gen_vs(Str *s, const Key *k)
{
    sp(s, "attribute vec4 a_pos;\nattribute vec3 a_nrm;\nattribute vec4 a_col;\n");
    for (int u = 0; u < FF_UNITS; u++)
        if (k->tex_on & (1u << u))
            sp(s, "attribute vec4 a_tc%d;\nuniform mat4 u_tm%d;\nvarying vec4 v_tc%d;\n", u, u, u);
    sp(s, "uniform mat4 u_mv;\nuniform mat4 u_proj;\nvarying vec4 v_col;\n");
    if (k->fog) sp(s, "uniform vec4 u_fog;\nvarying float v_fog;\n");
    if (k->lighting) {
        sp(s, "uniform mat3 u_nmat;\n"
              "uniform vec4 u_mamb, u_mdif, u_mspec, u_memi, u_lmamb;\n"
              "uniform float u_mshin;\n"
              "uniform vec4 u_lamb[%d], u_ldif[%d], u_lspec[%d], u_lpos[%d], u_lspot[%d], u_latt[%d];\n",
           FF_LIGHTS, FF_LIGHTS, FF_LIGHTS, FF_LIGHTS, FF_LIGHTS, FF_LIGHTS);
    }
    sp(s, "void main() {\n"
          "  vec4 eye = u_mv * a_pos;\n"
          "  gl_Position = u_proj * eye;\n"
          "  gl_PointSize = 1.0;\n"
          "  vec4 col = a_col;\n");
    if (k->lighting) {
        sp(s, "  vec3 N = normalize(u_nmat * a_nrm);\n"
              "  vec4 ma = %s;\n  vec4 md = %s;\n"
              "  vec3 c = u_memi.rgb + ma.rgb * u_lmamb.rgb;\n",
           k->color_material ? "col" : "u_mamb", k->color_material ? "col" : "u_mdif");
        for (int i = 0; i < FF_LIGHTS; i++) {
            if (!(k->lights & (1u << i))) continue;
            sp(s, "  {\n"
                  "    vec3 L; float att = 1.0;\n"
                  "    if (u_lpos[%d].w == 0.0) {\n"
                  "      L = normalize(u_lpos[%d].xyz);\n"
                  "    } else {\n"
                  "      vec3 d = u_lpos[%d].xyz - eye.xyz / eye.w;\n"
                  "      float dist = length(d);\n"
                  "      L = d / max(dist, 1e-6);\n"
                  "      att = 1.0 / max(u_latt[%d].x + u_latt[%d].y * dist + u_latt[%d].z * dist * dist, 1e-6);\n"
                  "      if (u_latt[%d].w > -1.5) {\n"
                  "        float sd = dot(-L, u_lspot[%d].xyz);\n"
                  "        att *= sd >= u_latt[%d].w ? pow(max(sd, 0.0), u_lspot[%d].w) : 0.0;\n"
                  "      }\n"
                  "    }\n"
                  "    float nl = max(dot(N, L), 0.0);\n"
                  "    float nh = max(dot(N, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0);\n"
                  "    float spf = nl > 0.0 ? (u_mshin > 0.0 ? pow(nh, u_mshin) : 1.0) : 0.0;\n"
                  "    c += att * (ma.rgb * u_lamb[%d].rgb + nl * md.rgb * u_ldif[%d].rgb +\n"
                  "                spf * u_mspec.rgb * u_lspec[%d].rgb);\n"
                  "  }\n",
               i, i, i, i, i, i, i, i, i, i, i, i, i);
        }
        sp(s, "  col = vec4(clamp(c, 0.0, 1.0), clamp(md.a, 0.0, 1.0));\n");
    }
    sp(s, "  v_col = col;\n");
    for (int u = 0; u < FF_UNITS; u++)
        if (k->tex_on & (1u << u)) sp(s, "  v_tc%d = u_tm%d * a_tc%d;\n", u, u, u);
    if (k->fog) {
        sp(s, "  float fc = abs(eye.z / eye.w);\n");
        if (k->fog == 1) sp(s, "  v_fog = clamp((u_fog.z - fc) * u_fog.w, 0.0, 1.0);\n");
        else if (k->fog == 2) sp(s, "  v_fog = clamp(exp(-u_fog.x * fc), 0.0, 1.0);\n");
        else sp(s, "  float fd = u_fog.x * fc;\n  v_fog = clamp(exp(-fd * fd), 0.0, 1.0);\n");
    }
    sp(s, "}\n");
}

static const char *src_expr(int u, uint8_t src, char *buf, size_t n, const Key *k)
{
    switch (src) {
    case 0: snprintf(buf, n, "t%d", u); break;
    case 1: snprintf(buf, n, "u_envc%d", u); break;
    case 2: snprintf(buf, n, "prim"); break;
    case 3: snprintf(buf, n, "prev"); break;
    default: {
        int t = src - 4;
        if (k->tex_on & (1u << t)) snprintf(buf, n, "t%d", t);
        else snprintf(buf, n, "vec4(1.0)");
    }
    }
    return buf;
}

static void gen_combine(Str *s, int u, const Key *k)
{
    const uint8_t *c = k->comb[u];
    char b[32];
    for (int i = 0; i < 3; i++) {
        const char *e = src_expr(u, c[2 + i], b, sizeof(b), k);
        switch (c[5 + i]) {
        case 0: sp(s, "    vec3 cr%d = %s.rgb;\n", i, e); break;
        case 1: sp(s, "    vec3 cr%d = vec3(1.0) - %s.rgb;\n", i, e); break;
        case 2: sp(s, "    vec3 cr%d = vec3(%s.a);\n", i, e); break;
        default: sp(s, "    vec3 cr%d = vec3(1.0 - %s.a);\n", i, e); break;
        }
        e = src_expr(u, c[8 + i], b, sizeof(b), k);
        if (c[11 + i] == 3) sp(s, "    float ca%d = 1.0 - %s.a;\n", i, e);
        else sp(s, "    float ca%d = %s.a;\n", i, e);
    }
    static const char *frgb[] = {
        "cr0", "cr0 * cr1", "cr0 + cr1", "cr0 + cr1 - vec3(0.5)",
        "cr0 * cr2 + cr1 * (vec3(1.0) - cr2)", "cr0 - cr1",
        "vec3(4.0 * dot(cr0 - vec3(0.5), cr1 - vec3(0.5)))",
        "vec3(4.0 * dot(cr0 - vec3(0.5), cr1 - vec3(0.5)))"
    };
    static const char *fa[] = {
        "ca0", "ca0 * ca1", "ca0 + ca1", "ca0 + ca1 - 0.5",
        "ca0 * ca2 + ca1 * (1.0 - ca2)", "ca0 - ca1", "ca0 * ca1", "ca0 * ca1"
    };
    static const char *scl[] = { "1.0", "2.0", "4.0" };
    sp(s, "    vec3 rc = clamp((%s) * %s, 0.0, 1.0);\n", frgb[c[0] & 7], scl[c[14] % 3]);
    if (c[0] == 7)
        sp(s, "    prev = vec4(rc, rc.r);\n");
    else
        sp(s, "    prev = vec4(rc, clamp((%s) * %s, 0.0, 1.0));\n", fa[c[1] & 7], scl[c[15] % 3]);
}

static void gen_fs(Str *s, const Key *k)
{
    sp(s, "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n");
    sp(s, "varying vec4 v_col;\n");
    for (int u = 0; u < FF_UNITS; u++)
        if (k->tex_on & (1u << u))
            sp(s, "uniform sampler2D u_s%d;\nuniform vec4 u_envc%d;\nvarying vec4 v_tc%d;\n", u, u, u);
    if (k->fog) sp(s, "uniform vec4 u_fogc;\nvarying float v_fog;\n");
    if (k->alpha) sp(s, "uniform float u_aref;\n");
    sp(s, "void main() {\n  vec4 prim = v_col;\n  vec4 prev = prim;\n");
    for (int u = 0; u < FF_UNITS; u++)
        if (k->tex_on & (1u << u))
            sp(s, "  vec4 t%d = texture2D(u_s%d, v_tc%d.xy / (v_tc%d.w == 0.0 ? 1.0 : v_tc%d.w));\n",
               u, u, u, u, u);
    for (int u = 0; u < FF_UNITS; u++) {
        if (!(k->tex_on & (1u << u))) continue;
        int f = k->fmt[u];
        int hasC = f != FMT_ALPHA;
        int hasA = f == FMT_ALPHA || f == FMT_LUM_ALPHA || f == FMT_RGBA;
        sp(s, "  {\n");
        switch (k->env[u]) {
        case ENV_REPLACE:
            if (hasC) sp(s, "    prev.rgb = t%d.rgb;\n", u);
            if (hasA) sp(s, "    prev.a = t%d.a;\n", u);
            break;
        case ENV_DECAL:
            if (f == FMT_RGB) sp(s, "    prev.rgb = t%d.rgb;\n", u);
            else if (f == FMT_RGBA) sp(s, "    prev.rgb = mix(prev.rgb, t%d.rgb, t%d.a);\n", u, u);
            break;
        case ENV_BLEND:
            if (hasC) sp(s, "    prev.rgb = mix(prev.rgb, u_envc%d.rgb, t%d.rgb);\n", u, u);
            if (hasA) sp(s, "    prev.a = prev.a * t%d.a;\n", u);
            break;
        case ENV_ADD:
            if (hasC) sp(s, "    prev.rgb = clamp(prev.rgb + t%d.rgb, 0.0, 1.0);\n", u);
            if (hasA) sp(s, "    prev.a = prev.a * t%d.a;\n", u);
            break;
        case ENV_COMBINE:
            gen_combine(s, u, k);
            break;
        default: /* MODULATE */
            if (hasC) sp(s, "    prev.rgb = prev.rgb * t%d.rgb;\n", u);
            if (hasA) sp(s, "    prev.a = prev.a * t%d.a;\n", u);
            break;
        }
        sp(s, "  }\n");
    }
    if (k->alpha) {
        static const char *cmp[] = { "false", "a < r", "a == r", "a <= r", "a > r", "a != r", "a >= r", "true" };
        sp(s, "  { float a = prev.a; float r = u_aref; if (!(%s)) discard; }\n", cmp[(k->alpha - 1) & 7]);
    }
    if (k->fog) sp(s, "  prev.rgb = mix(u_fogc.rgb, prev.rgb, v_fog);\n");
    sp(s, "  gl_FragColor = prev;\n}\n");
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = es.CreateShader(type);
    es.ShaderSource(sh, 1, &src, NULL);
    es.CompileShader(sh);
    GLint ok = 0;
    es.GetShaderiv(sh, GLES_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        es.GetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "[ff] shader compile failed:\n%s\n--- source ---\n%s\n", log, src);
        es.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static Prog *make_prog(const Key *k, uint32_t h)
{
    Str vs = {0}, fs = {0};
    gen_vs(&vs, k);
    gen_fs(&fs, k);
    GLuint v = compile(GLES_VERTEX_SHADER, vs.buf);
    GLuint f = compile(GLES_FRAGMENT_SHADER, fs.buf);
    free(vs.buf); free(fs.buf);
    if (!v || !f) return NULL;
    GLuint p = es.CreateProgram();
    es.AttachShader(p, v);
    es.AttachShader(p, f);
    es.BindAttribLocation(p, 0, "a_pos");
    es.BindAttribLocation(p, 1, "a_nrm");
    es.BindAttribLocation(p, 2, "a_col");
    for (int u = 0; u < FF_UNITS; u++) {
        char n[16];
        snprintf(n, sizeof(n), "a_tc%d", u);
        es.BindAttribLocation(p, (GLuint)(3 + u), n);
    }
    es.LinkProgram(p);
    es.DeleteShader(v);
    es.DeleteShader(f);
    GLint ok = 0;
    es.GetProgramiv(p, GLES_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = "";
        es.GetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[ff] program link failed: %s\n", log);
        return NULL;
    }
    Prog *pr;
    if (g_nprogs < FF_PROGRAMS) pr = &g_progs[g_nprogs++];
    else { pr = &g_progs[h % FF_PROGRAMS]; }
    memset(pr, 0, sizeof(*pr));
    pr->key = *k;
    pr->hash = h;
    pr->prog = p;
#define LOC(f, n) pr->f = es.GetUniformLocation(p, n)
    LOC(u_mv, "u_mv"); LOC(u_proj, "u_proj"); LOC(u_nmat, "u_nmat");
    LOC(u_fogc, "u_fogc"); LOC(u_fog, "u_fog"); LOC(u_aref, "u_aref");
    LOC(u_mamb, "u_mamb"); LOC(u_mdif, "u_mdif"); LOC(u_mspec, "u_mspec");
    LOC(u_memi, "u_memi"); LOC(u_mshin, "u_mshin"); LOC(u_lmamb, "u_lmamb");
    LOC(u_lamb, "u_lamb"); LOC(u_ldif, "u_ldif"); LOC(u_lspec, "u_lspec");
    LOC(u_lpos, "u_lpos"); LOC(u_lspot, "u_lspot"); LOC(u_latt, "u_latt");
#undef LOC
    es.UseProgram(p);
    S.cur_prog = p;
    for (int u = 0; u < FF_UNITS; u++) {
        char n[16];
        snprintf(n, sizeof(n), "u_tm%d", u);
        pr->u_tm[u] = es.GetUniformLocation(p, n);
        snprintf(n, sizeof(n), "u_envc%d", u);
        pr->u_envc[u] = es.GetUniformLocation(p, n);
        snprintf(n, sizeof(n), "u_s%d", u);
        GLint ls = es.GetUniformLocation(p, n);
        if (ls >= 0) es.Uniform1i(ls, u);
    }
    if (getenv("RECOMP_FF_LOG"))
        fprintf(stderr, "[ff] program %d: tex=%x lights=%x fog=%d alpha=%d\n",
                g_nprogs, k->tex_on, k->lighting ? k->lights : 0, k->fog, k->alpha);
    return pr;
}

static Prog *get_prog(void)
{
    static Prog *last;
    Key k;
    build_key(&k);
    if (last && !memcmp(&last->key, &k, sizeof(k))) return last;
    uint32_t h = key_hash(&k);
    for (int i = 0; i < g_nprogs; i++)
        if (g_progs[i].hash == h && !memcmp(&g_progs[i].key, &k, sizeof(k)))
            return last = &g_progs[i];
    Prog *p = make_prog(&k, h);
    if (p) last = p;
    return p;
}

/* Inverse transpose of the modelview's upper 3x3 (column-major result). */
static void normal_matrix(const float *m, float *out)
{
    float a = m[0], b = m[4], c = m[8];
    float d = m[1], e = m[5], f = m[9];
    float g = m[2], h = m[6], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (fabsf(det) < 1e-12f) det = 1e-12f;
    float inv = 1.0f / det;
    /* inverse(M)^T = cofactor(M) / det; stored column-major. */
    out[0] = A * inv;
    out[1] = B * inv;
    out[2] = C * inv;
    out[3] = -(b * i - c * h) * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = -(a * h - b * g) * inv;
    out[6] = (b * f - c * e) * inv;
    out[7] = -(a * f - c * d) * inv;
    out[8] = (a * e - b * d) * inv;
}

static void upload(Prog *p)
{
    if (S.cur_prog != p->prog) { es.UseProgram(p->prog); S.cur_prog = p->prog; }
    const Key *k = &p->key;
    if (p->seen[G_MTX] != g_serial[G_MTX]) {
        p->seen[G_MTX] = g_serial[G_MTX];
        es.UniformMatrix4fv(p->u_mv, 1, GL_FALSE, S.mv.m[S.mv.top]);
        es.UniformMatrix4fv(p->u_proj, 1, GL_FALSE, S.proj.m[S.proj.top]);
        for (int u = 0; u < FF_UNITS; u++)
            if (p->u_tm[u] >= 0)
                es.UniformMatrix4fv(p->u_tm[u], 1, GL_FALSE, S.tex[u].m[S.tex[u].top]);
        if (k->lighting) {
            float nm[9];
            normal_matrix(S.mv.m[S.mv.top], nm);
            es.UniformMatrix3fv(p->u_nmat, 1, GL_FALSE, nm);
        }
    }
    if (k->lighting && p->seen[G_LIGHT] != g_serial[G_LIGHT]) {
        p->seen[G_LIGHT] = g_serial[G_LIGHT];
        float amb[FF_LIGHTS * 4], dif[FF_LIGHTS * 4], spec[FF_LIGHTS * 4];
        float pos[FF_LIGHTS * 4], spot[FF_LIGHTS * 4], att[FF_LIGHTS * 4];
        for (int i = 0; i < FF_LIGHTS; i++) {
            const Light *l = &S.light[i];
            memcpy(amb + i * 4, l->amb, 16);
            memcpy(dif + i * 4, l->dif, 16);
            memcpy(spec + i * 4, l->spec, 16);
            memcpy(pos + i * 4, l->pos, 16);
            float dl = sqrtf(l->spot_dir[0] * l->spot_dir[0] + l->spot_dir[1] * l->spot_dir[1] +
                             l->spot_dir[2] * l->spot_dir[2]);
            if (dl < 1e-6f) dl = 1.0f;
            spot[i * 4 + 0] = l->spot_dir[0] / dl;
            spot[i * 4 + 1] = l->spot_dir[1] / dl;
            spot[i * 4 + 2] = l->spot_dir[2] / dl;
            spot[i * 4 + 3] = l->spot_exp;
            att[i * 4 + 0] = l->att[0];
            att[i * 4 + 1] = l->att[1];
            att[i * 4 + 2] = l->att[2];
            att[i * 4 + 3] = l->spot_cut >= 180.0f ? -2.0f
                                                    : cosf(l->spot_cut * 3.14159265f / 180.0f);
        }
        es.Uniform4fv(p->u_lamb, FF_LIGHTS, amb);
        es.Uniform4fv(p->u_ldif, FF_LIGHTS, dif);
        es.Uniform4fv(p->u_lspec, FF_LIGHTS, spec);
        es.Uniform4fv(p->u_lpos, FF_LIGHTS, pos);
        es.Uniform4fv(p->u_lspot, FF_LIGHTS, spot);
        es.Uniform4fv(p->u_latt, FF_LIGHTS, att);
    }
    if (k->lighting && p->seen[G_MAT] != g_serial[G_MAT]) {
        p->seen[G_MAT] = g_serial[G_MAT];
        es.Uniform4fv(p->u_mamb, 1, S.mat_amb);
        es.Uniform4fv(p->u_mdif, 1, S.mat_dif);
        es.Uniform4fv(p->u_mspec, 1, S.mat_spec);
        es.Uniform4fv(p->u_memi, 1, S.mat_emi);
        es.Uniform1f(p->u_mshin, S.mat_shin);
        es.Uniform4fv(p->u_lmamb, 1, S.lm_amb);
    }
    if (k->fog && p->seen[G_FOG] != g_serial[G_FOG]) {
        p->seen[G_FOG] = g_serial[G_FOG];
        float range = S.fog_end - S.fog_start;
        float f[4] = { S.fog_density, S.fog_start, S.fog_end,
                       fabsf(range) > 1e-6f ? 1.0f / range : 0.0f };
        es.Uniform4fv(p->u_fog, 1, f);
        es.Uniform4fv(p->u_fogc, 1, S.fog_color);
    }
    if (k->tex_on && p->seen[G_ENV] != g_serial[G_ENV]) {
        p->seen[G_ENV] = g_serial[G_ENV];
        for (int u = 0; u < FF_UNITS; u++)
            if (p->u_envc[u] >= 0) es.Uniform4fv(p->u_envc[u], 1, S.env[u].color);
    }
    if (k->alpha && p->seen[G_AREF] != g_serial[G_AREF]) {
        p->seen[G_AREF] = g_serial[G_AREF];
        es.Uniform1f(p->u_aref, S.alpha_ref);
    }
}

/* ---- vertex attributes ------------------------------------------------- */
static void attr_enable(GLuint idx, int on)
{
    unsigned bit = 1u << idx;
    if (on && !(S.attr_on & bit)) { es.EnableVertexAttribArray(idx); S.attr_on |= bit; }
    if (!on && (S.attr_on & bit)) { es.DisableVertexAttribArray(idx); S.attr_on &= ~bit; }
}

static GLboolean norm_for(GLenum type, int is_color, int is_normal)
{
    if (type == GL_FLOAT || type == GL_FIXED) return GL_FALSE;
    if (is_color || is_normal) return GL_TRUE;
    return GL_FALSE;
}

static int setup_attribs(const Key *k)
{
    if (!S.vtx.on || !S.vtx.ptr) return 0;
    attr_enable(0, 1);
    es.VertexAttribPointer(0, S.vtx.size, S.vtx.type, GL_FALSE, S.vtx.stride, S.vtx.ptr);

    if (k->lighting && S.nrm.on && S.nrm.ptr) {
        attr_enable(1, 1);
        es.VertexAttribPointer(1, 3, S.nrm.type, norm_for(S.nrm.type, 0, 1), S.nrm.stride, S.nrm.ptr);
    } else {
        attr_enable(1, 0);
        es.VertexAttrib4f(1, S.normal[0], S.normal[1], S.normal[2], 0.0f);
    }

    if (S.col.on && S.col.ptr) {
        attr_enable(2, 1);
        es.VertexAttribPointer(2, S.col.size, S.col.type, norm_for(S.col.type, 1, 0),
                               S.col.stride, S.col.ptr);
    } else {
        attr_enable(2, 0);
        es.VertexAttrib4f(2, S.color[0], S.color[1], S.color[2], S.color[3]);
    }

    for (int u = 0; u < FF_UNITS; u++) {
        GLuint idx = (GLuint)(3 + u);
        if ((k->tex_on & (1u << u)) && S.tc[u].on && S.tc[u].ptr) {
            attr_enable(idx, 1);
            es.VertexAttribPointer(idx, S.tc[u].size, S.tc[u].type, GL_FALSE,
                                   S.tc[u].stride, S.tc[u].ptr);
        } else {
            attr_enable(idx, 0);
            if (k->tex_on & (1u << u))
                es.VertexAttrib4f(idx, S.texcoord[0], S.texcoord[1], S.texcoord[2], S.texcoord[3]);
        }
    }
    return 1;
}

static Prog *prepare_draw(void)
{
    Prog *p = get_prog();
    if (!p) return NULL;
    if (!setup_attribs(&p->key)) return NULL;
    upload(p);
    return p;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count <= 0 || mode > GL_TRIANGLE_FAN) return;
    if (!prepare_draw()) return;
    es.DrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    if (count <= 0 || !indices || mode > GL_TRIANGLE_FAN) return;
    if (!prepare_draw()) return;
    es.DrawElements(mode, count, type, indices);
}

/* ---- capabilities ------------------------------------------------------ */
static int *misc_cap(GLenum cap, int create)
{
    for (int i = 0; i < 8; i++)
        if (S.misc_cap_id[i] == cap && (S.misc_caps[i] >= 0)) return &S.misc_caps[i];
    if (!create) return NULL;
    for (int i = 0; i < 8; i++)
        if (S.misc_cap_id[i] == 0) { S.misc_cap_id[i] = cap; S.misc_caps[i] = 0; return &S.misc_caps[i]; }
    return NULL;
}

static int is_es_cap(GLenum cap)
{
    switch (cap) {
    case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST: case GL_DITHER:
    case GL_POLYGON_OFFSET_FILL: case GL_SAMPLE_ALPHA_TO_COVERAGE:
    case GL_SAMPLE_COVERAGE: case GL_SCISSOR_TEST: case GL_STENCIL_TEST:
        return 1;
    default:
        return 0;
    }
}

static void set_cap(GLenum cap, int on)
{
    if (is_es_cap(cap)) { if (on) es.Enable(cap); else es.Disable(cap); return; }
    switch (cap) {
    case GL_TEXTURE_2D: S.tex2d[S.active] = on; return;
    case GL_LIGHTING: S.lighting = on; return;
    case GL_COLOR_MATERIAL: S.color_material = on; return;
    case GL_FOG: S.fog = on; return;
    case GL_ALPHA_TEST: S.alpha_test = on; return;
    case GL_NORMALIZE: case GL_RESCALE_NORMAL: S.normalize = on; return;
    default: break;
    }
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FF_LIGHTS) {
        S.light[cap - GL_LIGHT0].on = on;
        return;
    }
    int *m = misc_cap(cap, 1);
    if (m) *m = on;
}

void glEnable(GLenum cap) { set_cap(cap, 1); }
void glDisable(GLenum cap) { set_cap(cap, 0); }

GLboolean glIsEnabled(GLenum cap)
{
    if (is_es_cap(cap)) return es.IsEnabled(cap);
    switch (cap) {
    case GL_TEXTURE_2D: return (GLboolean)S.tex2d[S.active];
    case GL_LIGHTING: return (GLboolean)S.lighting;
    case GL_COLOR_MATERIAL: return (GLboolean)S.color_material;
    case GL_FOG: return (GLboolean)S.fog;
    case GL_ALPHA_TEST: return (GLboolean)S.alpha_test;
    case GL_NORMALIZE: case GL_RESCALE_NORMAL: return (GLboolean)S.normalize;
    case GL_VERTEX_ARRAY: return (GLboolean)S.vtx.on;
    case GL_NORMAL_ARRAY: return (GLboolean)S.nrm.on;
    case GL_COLOR_ARRAY: return (GLboolean)S.col.on;
    case GL_TEXTURE_COORD_ARRAY: return (GLboolean)S.tc[S.client].on;
    default: break;
    }
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FF_LIGHTS) return (GLboolean)S.light[cap - GL_LIGHT0].on;
    int *m = misc_cap(cap, 0);
    return (GLboolean)(m ? *m : 0);
}

static ClientArray *client_array(GLenum array)
{
    switch (array) {
    case GL_VERTEX_ARRAY: return &S.vtx;
    case GL_NORMAL_ARRAY: return &S.nrm;
    case GL_COLOR_ARRAY: return &S.col;
    case GL_TEXTURE_COORD_ARRAY: return &S.tc[S.client];
    default: return NULL;
    }
}

void glEnableClientState(GLenum array) { ClientArray *a = client_array(array); if (a) a->on = 1; }
void glDisableClientState(GLenum array) { ClientArray *a = client_array(array); if (a) a->on = 0; }

static void set_array(ClientArray *a, GLint size, GLenum type, GLsizei stride, const void *p)
{
    a->size = size; a->type = type; a->stride = stride; a->ptr = p;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_array(&S.vtx, size, type, stride, p); }
void glNormalPointer(GLenum type, GLsizei stride, const void *p) { set_array(&S.nrm, 3, type, stride, p); }
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_array(&S.col, size, type, stride, p); }
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_array(&S.tc[S.client], size, type, stride, p); }

void APIENTRY ff_ActiveTexture(GLenum unit)
{
    int u = (int)unit - GL_TEXTURE0;
    if (u < 0 || u >= FF_UNITS) return;
    S.active = u;
    es.ActiveTexture(unit);
}

void APIENTRY ff_ClientActiveTexture(GLenum unit)
{
    int u = (int)unit - GL_TEXTURE0;
    if (u >= 0 && u < FF_UNITS) S.client = u;
}

/* The GLES1 game has no glBufferData, so client arrays always come from
 * guest memory.  Keep ES on buffer 0 regardless of what is bound. */
void APIENTRY ff_BindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }

/* ---- simple state ------------------------------------------------------ */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { es.ClearColor(r, g, b, a); }
void glClearDepth(GLclampd d) { es.ClearDepthf((GLfloat)d); }
void glClear(GLbitfield mask) { es.Clear(mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)); }
void glBlendFunc(GLenum s, GLenum d) { es.BlendFunc(s, d); }
void glDepthFunc(GLenum f) { es.DepthFunc(f); }
void glDepthMask(GLboolean f) { es.DepthMask(f); }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { es.ColorMask(r, g, b, a); }
void glCullFace(GLenum m) { es.CullFace(m); }
void glFrontFace(GLenum m) { es.FrontFace(m); }
void glFinish(void) { es.Finish(); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { es.Viewport(x, y, w, h); }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { es.Scissor(x, y, w, h); }
void glShadeModel(GLenum m) { S.shade_model = m; }
void glReadBuffer(GLenum m) { (void)m; }
void glPushAttrib(GLbitfield m) { (void)m; }
void glPopAttrib(void) {}
void glPushClientAttrib(GLbitfield m) { (void)m; }
void glPopClientAttrib(void) {}

void glHint(GLenum target, GLenum mode)
{
    if (target == GL_GENERATE_MIPMAP_HINT) es.Hint(target, mode);
}

void glPixelStorei(GLenum pname, GLint v)
{
    if (pname == GL_UNPACK_ALIGNMENT || pname == GL_PACK_ALIGNMENT) es.PixelStorei(pname, v);
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    if (func < GL_NEVER || func > GL_ALWAYS) return;
    S.alpha_func = func;
    S.alpha_ref = ref < 0.0f ? 0.0f : ref > 1.0f ? 1.0f : ref;
    DIRTY(G_AREF);
}

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    S.color[0] = r; S.color[1] = g; S.color[2] = b; S.color[3] = a;
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

GLenum glGetError(void) { return es.GetError(); }
const GLubyte *glGetString(GLenum name) { return es.GetString(name); }

void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *px)
{
    if (fmt == GL_RGBA && type == GL_UNSIGNED_BYTE) { es.ReadPixels(x, y, w, h, fmt, type, px); return; }
    if (fmt == GL_RGB && type == GL_UNSIGNED_BYTE && w > 0 && h > 0) {
        unsigned char *tmp = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
        if (!tmp) return;
        GLint pack = 4;
        es.GetIntegerv(GL_PACK_ALIGNMENT, &pack);
        es.PixelStorei(GL_PACK_ALIGNMENT, 4);
        es.ReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        es.PixelStorei(GL_PACK_ALIGNMENT, pack);
        size_t row = ((size_t)w * 3 + (size_t)(pack - 1)) & ~(size_t)(pack - 1);
        for (GLsizei j = 0; j < h; j++)
            for (GLsizei i = 0; i < w; i++)
                memcpy((unsigned char *)px + (size_t)j * row + (size_t)i * 3,
                       tmp + ((size_t)j * (size_t)w + (size_t)i) * 4, 3);
        free(tmp);
    }
}

/* ---- matrices ---------------------------------------------------------- */
void glMatrixMode(GLenum mode)
{
    if (mode == GL_MODELVIEW || mode == GL_PROJECTION || mode == GL_TEXTURE) S.mode = mode;
}

void glLoadIdentity(void) { m_ident(cur_mat()); DIRTY(G_MTX); }
void glLoadMatrixf(const GLfloat *m) { if (m) { memcpy(cur_mat(), m, 16 * sizeof(float)); DIRTY(G_MTX); } }

void glPushMatrix(void)
{
    MatStack *s = cur_stack();
    if (s->top + 1 >= FF_STACK) return;
    memcpy(s->m[s->top + 1], s->m[s->top], 16 * sizeof(float));
    s->top++;
}

void glPopMatrix(void)
{
    MatStack *s = cur_stack();
    if (s->top > 0) { s->top--; DIRTY(G_MTX); }
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    float t[16];
    m_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    mult_cur(t);
}

void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z)
{
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-12f) return;
    x /= len; y /= len; z /= len;
    float r = a * 3.14159265358979f / 180.0f;
    float c = cosf(r), s = sinf(r), ic = 1.0f - c;
    float m[16];
    m_ident(m);
    m[0] = x * x * ic + c;     m[4] = x * y * ic - z * s; m[8] = x * z * ic + y * s;
    m[1] = y * x * ic + z * s; m[5] = y * y * ic + c;     m[9] = y * z * ic - x * s;
    m[2] = x * z * ic - y * s; m[6] = y * z * ic + x * s; m[10] = z * z * ic + c;
    mult_cur(m);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float m[16];
    m_ident(m);
    m[0] = (float)(2.0 / (r - l));
    m[5] = (float)(2.0 / (t - b));
    m[10] = (float)(-2.0 / (f - n));
    m[12] = (float)(-(r + l) / (r - l));
    m[13] = (float)(-(t + b) / (t - b));
    m[14] = (float)(-(f + n) / (f - n));
    mult_cur(m);
}

/* ---- lights, materials, fog ------------------------------------------- */
static void xform_point(const float *m, const float *in, float *out)
{
    for (int r = 0; r < 4; r++)
        out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r] * in[3];
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *v)
{
    int i = (int)light - GL_LIGHT0;
    if (i < 0 || i >= FF_LIGHTS || !v) return;
    Light *l = &S.light[i];
    const float *mv = S.mv.m[S.mv.top];
    switch (pname) {
    case GL_AMBIENT: memcpy(l->amb, v, 16); break;
    case GL_DIFFUSE: memcpy(l->dif, v, 16); break;
    case GL_SPECULAR: memcpy(l->spec, v, 16); break;
    case GL_POSITION: xform_point(mv, v, l->pos); break;
    case GL_SPOT_DIRECTION:
        for (int r = 0; r < 3; r++)
            l->spot_dir[r] = mv[r] * v[0] + mv[4 + r] * v[1] + mv[8 + r] * v[2];
        break;
    case GL_SPOT_EXPONENT: l->spot_exp = v[0]; break;
    case GL_SPOT_CUTOFF: l->spot_cut = v[0]; break;
    case GL_CONSTANT_ATTENUATION: l->att[0] = v[0]; break;
    case GL_LINEAR_ATTENUATION: l->att[1] = v[0]; break;
    case GL_QUADRATIC_ATTENUATION: l->att[2] = v[0]; break;
    default: return;
    }
    DIRTY(G_LIGHT);
}

void glLightf(GLenum light, GLenum pname, GLfloat v) { glLightfv(light, pname, &v); }

void glLightModelfv(GLenum pname, const GLfloat *v)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT && v) { memcpy(S.lm_amb, v, 16); DIRTY(G_MAT); }
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *v)
{
    (void)face;
    if (!v) return;
    switch (pname) {
    case GL_AMBIENT: memcpy(S.mat_amb, v, 16); break;
    case GL_DIFFUSE: memcpy(S.mat_dif, v, 16); break;
    case GL_AMBIENT_AND_DIFFUSE: memcpy(S.mat_amb, v, 16); memcpy(S.mat_dif, v, 16); break;
    case GL_SPECULAR: memcpy(S.mat_spec, v, 16); break;
    case GL_EMISSION: memcpy(S.mat_emi, v, 16); break;
    case GL_SHININESS: S.mat_shin = v[0]; break;
    default: return;
    }
    DIRTY(G_MAT);
}

void glMaterialf(GLenum face, GLenum pname, GLfloat v) { glMaterialfv(face, pname, &v); }

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *v)
{
    (void)face;
    if (!v) return;
    switch (pname) {
    case GL_AMBIENT: memcpy(v, S.mat_amb, 16); break;
    case GL_DIFFUSE: memcpy(v, S.mat_dif, 16); break;
    case GL_SPECULAR: memcpy(v, S.mat_spec, 16); break;
    case GL_EMISSION: memcpy(v, S.mat_emi, 16); break;
    case GL_SHININESS: v[0] = S.mat_shin; break;
    default: break;
    }
}

void glFogfv(GLenum pname, const GLfloat *v)
{
    if (!v) return;
    switch (pname) {
    case GL_FOG_MODE: S.fog_mode = (GLenum)v[0]; break;
    case GL_FOG_DENSITY: S.fog_density = v[0]; break;
    case GL_FOG_START: S.fog_start = v[0]; break;
    case GL_FOG_END: S.fog_end = v[0]; break;
    case GL_FOG_COLOR: memcpy(S.fog_color, v, 16); break;
    default: return;
    }
    DIRTY(G_FOG);
}

void glFogf(GLenum pname, GLfloat v)
{
    if (pname == GL_FOG_COLOR) return;
    glFogfv(pname, &v);
}

/* ---- texture environment ---------------------------------------------- */
static int env_mode(GLint v)
{
    switch (v) {
    case GL_REPLACE: return ENV_REPLACE;
    case GL_DECAL: return ENV_DECAL;
    case GL_BLEND: return ENV_BLEND;
    case GL_ADD: return ENV_ADD;
    case GL_COMBINE: return ENV_COMBINE;
    default: return ENV_MODULATE;
    }
}

static void tex_env(GLenum target, GLenum pname, const GLfloat *fv, GLint iv)
{
    if (target != GL_TEXTURE_ENV) return;
    TexEnv *e = &S.env[S.active];
    switch (pname) {
    case GL_TEXTURE_ENV_MODE: e->mode = env_mode(iv); break;
    case GL_TEXTURE_ENV_COLOR: if (fv) memcpy(e->color, fv, 16); DIRTY(G_ENV); break;
    case GL_COMBINE_RGB: e->comb_rgb = (GLenum)iv; break;
    case GL_COMBINE_ALPHA: e->comb_alpha = (GLenum)iv; break;
    case GL_SRC0_RGB: case GL_SRC1_RGB: case GL_SRC2_RGB:
        e->src_rgb[pname - GL_SRC0_RGB] = (GLenum)iv; break;
    case GL_SRC0_ALPHA: case GL_SRC1_ALPHA: case GL_SRC2_ALPHA:
        e->src_a[pname - GL_SRC0_ALPHA] = (GLenum)iv; break;
    case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB:
        e->op_rgb[pname - GL_OPERAND0_RGB] = (GLenum)iv; break;
    case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
        e->op_a[pname - GL_OPERAND0_ALPHA] = (GLenum)iv; break;
    case GL_RGB_SCALE: e->rgb_scale = fv ? fv[0] : (GLfloat)iv; break;
    case GL_ALPHA_SCALE: e->a_scale = fv ? fv[0] : (GLfloat)iv; break;
    default: break;
    }
}

void glTexEnvi(GLenum target, GLenum pname, GLint v) { tex_env(target, pname, NULL, v); }

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *v)
{
    if (!v) return;
    if (pname == GL_TEXTURE_ENV_COLOR) tex_env(target, pname, v, 0);
    else if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE) tex_env(target, pname, v, 0);
    else tex_env(target, pname, NULL, (GLint)v[0]);
}

void glGetTexEnviv(GLenum target, GLenum pname, GLint *v)
{
    (void)target;
    static const GLint modes[] = { GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD, GL_COMBINE };
    if (!v) return;
    if (pname == GL_TEXTURE_ENV_MODE) *v = modes[S.env[S.active].mode];
    else *v = 0;
}

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *v)
{
    (void)target;
    if (!v) return;
    if (pname == GL_TEXTURE_ENV_COLOR) memcpy(v, S.env[S.active].color, 16);
    else v[0] = 0.0f;
}

/* ---- textures ---------------------------------------------------------- */
static int fmt_class(GLenum fmt)
{
    switch (fmt) {
    case GL_ALPHA: return FMT_ALPHA;
    case GL_LUMINANCE: case 1: return FMT_LUM;
    case GL_LUMINANCE_ALPHA: case 2: return FMT_LUM_ALPHA;
    case GL_RGB: case 3: case 0x83F0 /* DXT1 RGB */: case 0x8D64 /* ETC1 */: return FMT_RGB;
    default: return FMT_RGBA;
    }
}

void glGenTextures(GLsizei n, GLuint *t) { es.GenTextures(n, t); }

void glDeleteTextures(GLsizei n, const GLuint *t)
{
    if (!t) return;
    for (GLsizei i = 0; i < n; i++) {
        TexInfo *ti = tex_info(t[i]);
        if (ti) memset(ti, 0, sizeof(*ti));
        for (int u = 0; u < FF_UNITS; u++)
            if (S.bound[u] == t[i]) S.bound[u] = 0;
    }
    es.DeleteTextures(n, t);
}

void glBindTexture(GLenum target, GLuint t)
{
    if (target != GL_TEXTURE_2D) return;
    S.bound[S.active] = t;
    es.BindTexture(target, t);
}

static void maybe_genmip(GLint level)
{
    TexInfo *ti = tex_info(S.bound[S.active]);
    if (level == 0 && ti && ti->genmip && ti->valid) es.GenerateMipmap(GL_TEXTURE_2D);
}

void glTexParameteri(GLenum target, GLenum pname, GLint v)
{
    if (target != GL_TEXTURE_2D) return;
    if (pname == GL_GENERATE_MIPMAP) {
        TexInfo *ti = tex_info(S.bound[S.active]);
        if (ti) {
            ti->genmip = v != 0;
            if (ti->genmip && ti->valid) es.GenerateMipmap(GL_TEXTURE_2D);
        }
        return;
    }
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && v == GL_CLAMP)
        v = GL_CLAMP_TO_EDGE;
    es.TexParameteri(target, pname, v);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat v)
{
    if (pname == GL_GENERATE_MIPMAP || pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T ||
        pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER) {
        glTexParameteri(target, pname, (GLint)v);
        return;
    }
    if (target == GL_TEXTURE_2D) es.TexParameterf(target, pname, v);
}

void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                  GLint border, GLenum fmt, GLenum type, const void *px)
{
    if (target != GL_TEXTURE_2D) return;
    /* ES requires internalformat == format for unsized formats. */
    GLint esfmt = (GLint)fmt;
    TexInfo *ti = tex_info(S.bound[S.active]);
    if (ti && level == 0) {
        ti->w = w; ti->h = h; ti->valid = 1;
        ti->fmt = fmt_class(fmt == GL_BGRA ? GL_RGBA : (GLenum)fmt);
    }
    (void)ifmt;
    es.TexImage2D(target, level, esfmt, w, h, border, fmt, type, px);
    maybe_genmip(level);
}

void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                     GLsizei h, GLenum fmt, GLenum type, const void *px)
{
    if (target != GL_TEXTURE_2D) return;
    es.TexSubImage2D(target, level, x, y, w, h, fmt, type, px);
    maybe_genmip(level);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum ifmt, GLint x, GLint y,
                      GLsizei w, GLsizei h, GLint border)
{
    if (target != GL_TEXTURE_2D) return;
    if (ifmt == 3) ifmt = GL_RGB;
    if (ifmt == 4) ifmt = GL_RGBA;
    TexInfo *ti = tex_info(S.bound[S.active]);
    if (ti && level == 0) { ti->w = w; ti->h = h; ti->valid = 1; ti->fmt = fmt_class(ifmt); }
    es.CopyTexImage2D(target, level, ifmt, x, y, w, h, border);
    maybe_genmip(level);
}

void APIENTRY ff_CompressedTexImage2D(GLenum target, GLint level, GLenum fmt,
                                      GLsizei w, GLsizei h, GLint border,
                                      GLsizei size, const void *data)
{
    if (target != GL_TEXTURE_2D) return;
    TexInfo *ti = tex_info(S.bound[S.active]);
    if (ti && level == 0) { ti->w = w; ti->h = h; ti->valid = 1; ti->fmt = fmt_class(fmt); }
    es.CompressedTexImage2D(target, level, fmt, w, h, border, size, data);
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *v)
{
    (void)target;
    if (!v) return;
    TexInfo *ti = tex_info(S.bound[S.active]);
    *v = 0;
    if (!ti || level != 0) return;
    if (pname == GL_TEXTURE_WIDTH) *v = ti->w;
    else if (pname == GL_TEXTURE_HEIGHT) *v = ti->h;
}

/* ES cannot read textures back without a framebuffer; diagnostics only. */
void glGetTexImage(GLenum target, GLint level, GLenum fmt, GLenum type, void *px)
{
    (void)target; (void)fmt; (void)type;
    TexInfo *ti = tex_info(S.bound[S.active]);
    if (px && ti && level == 0 && ti->w > 0 && ti->h > 0)
        memset(px, 0, (size_t)ti->w * (size_t)ti->h * 4);
}

/* ---- queries ----------------------------------------------------------- */
static int ff_get(GLenum pname, GLfloat *f, int *n)
{
    switch (pname) {
    case GL_MODELVIEW_MATRIX: memcpy(f, S.mv.m[S.mv.top], 64); *n = 16; return 1;
    case GL_PROJECTION_MATRIX: memcpy(f, S.proj.m[S.proj.top], 64); *n = 16; return 1;
    case GL_TEXTURE_MATRIX: memcpy(f, S.tex[S.active].m[S.tex[S.active].top], 64); *n = 16; return 1;
    case GL_CURRENT_COLOR: memcpy(f, S.color, 16); *n = 4; return 1;
    case GL_CURRENT_NORMAL: memcpy(f, S.normal, 12); *n = 3; return 1;
    case GL_CURRENT_TEXTURE_COORDS: memcpy(f, S.texcoord, 16); *n = 4; return 1;
    case GL_MATRIX_MODE: f[0] = (GLfloat)S.mode; *n = 1; return 1;
    case GL_ACTIVE_TEXTURE: f[0] = (GLfloat)(GL_TEXTURE0 + S.active); *n = 1; return 1;
    case GL_CLIENT_ACTIVE_TEXTURE: f[0] = (GLfloat)(GL_TEXTURE0 + S.client); *n = 1; return 1;
    case GL_MAX_TEXTURE_UNITS: f[0] = 2; *n = 1; return 1;
    case GL_MAX_LIGHTS: f[0] = FF_LIGHTS; *n = 1; return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: case GL_MAX_PROJECTION_STACK_DEPTH:
    case GL_MAX_TEXTURE_STACK_DEPTH: f[0] = FF_STACK; *n = 1; return 1;
    case GL_MODELVIEW_STACK_DEPTH: f[0] = (GLfloat)(S.mv.top + 1); *n = 1; return 1;
    case GL_PROJECTION_STACK_DEPTH: f[0] = (GLfloat)(S.proj.top + 1); *n = 1; return 1;
    case GL_ALPHA_TEST_FUNC: f[0] = (GLfloat)S.alpha_func; *n = 1; return 1;
    case GL_ALPHA_TEST_REF: f[0] = S.alpha_ref; *n = 1; return 1;
    case GL_SHADE_MODEL: f[0] = (GLfloat)S.shade_model; *n = 1; return 1;
    case GL_FOG_COLOR: memcpy(f, S.fog_color, 16); *n = 4; return 1;
    case GL_FOG_DENSITY: f[0] = S.fog_density; *n = 1; return 1;
    case GL_FOG_START: f[0] = S.fog_start; *n = 1; return 1;
    case GL_FOG_END: f[0] = S.fog_end; *n = 1; return 1;
    case GL_FOG_MODE: f[0] = (GLfloat)S.fog_mode; *n = 1; return 1;
    case GL_LIGHT_MODEL_AMBIENT: memcpy(f, S.lm_amb, 16); *n = 4; return 1;
    case GL_ARRAY_BUFFER_BINDING: case GL_ELEMENT_ARRAY_BUFFER_BINDING: f[0] = 0; *n = 1; return 1;
    case GL_TEXTURE_BINDING_2D: f[0] = (GLfloat)S.bound[S.active]; *n = 1; return 1;
    default: return 0;
    }
}

void glGetFloatv(GLenum pname, GLfloat *v)
{
    GLfloat f[16];
    int n = 0;
    if (!v) return;
    if (ff_get(pname, f, &n)) { memcpy(v, f, (size_t)n * sizeof(GLfloat)); return; }
    es.GetFloatv(pname, v);
}

void glGetIntegerv(GLenum pname, GLint *v)
{
    GLfloat f[16];
    int n = 0;
    if (!v) return;
    if (ff_get(pname, f, &n)) {
        for (int i = 0; i < n; i++) v[i] = (GLint)f[i];
        return;
    }
    es.GetIntegerv(pname, v);
}

void glGetBooleanv(GLenum pname, GLboolean *v)
{
    if (!v) return;
    es.GetBooleanv(pname, v);
}

/* ---- immediate mode (movie quad) -------------------------------------- */
#define IM_MAX 64
static struct { GLenum mode; int n; float v[IM_MAX][4]; float tc[2]; } g_im;

void glBegin(GLenum mode) { g_im.mode = mode; g_im.n = 0; }
void glTexCoord2f(GLfloat s, GLfloat t) { g_im.tc[0] = s; g_im.tc[1] = t; }

void glVertex2i(GLint x, GLint y)
{
    if (g_im.n >= IM_MAX) return;
    float *v = g_im.v[g_im.n++];
    v[0] = (float)x; v[1] = (float)y; v[2] = g_im.tc[0]; v[3] = g_im.tc[1];
}

void glEnd(void)
{
    float tri[IM_MAX * 2][4];
    int n = 0;
    GLenum mode = g_im.mode;
    if (mode == GL_QUADS) {
        for (int q = 0; q + 3 < g_im.n; q += 4) {
            static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            for (int i = 0; i < 6; i++) memcpy(tri[n++], g_im.v[q + idx[i]], sizeof(tri[0]));
        }
        mode = GL_TRIANGLES;
    } else {
        memcpy(tri, g_im.v, (size_t)g_im.n * sizeof(tri[0]));
        n = g_im.n;
    }
    if (n <= 0) return;
    ClientArray vtx = S.vtx, nrm = S.nrm, col = S.col, tc[FF_UNITS];
    memcpy(tc, S.tc, sizeof(tc));
    S.vtx = (ClientArray){ 2, GL_FLOAT, 16, &tri[0][0], 1 };
    S.nrm.on = 0;
    S.col.on = 0;
    for (int u = 0; u < FF_UNITS; u++) S.tc[u].on = 0;
    S.tc[0] = (ClientArray){ 2, GL_FLOAT, 16, &tri[0][2], 1 };
    glDrawArrays(mode, 0, n);
    S.vtx = vtx; S.nrm = nrm; S.col = col;
    memcpy(S.tc, tc, sizeof(tc));
}

/* ---- overlay ----------------------------------------------------------- */
void ff_overlay_rect(int x, int y, int w, int h, float r, float g, float b, float a)
{
    if (w <= 0 || h <= 0 || S.surf_h <= 0) return;
    GLboolean sc = es.IsEnabled(GL_SCISSOR_TEST);
    GLint box[4];
    GLfloat cc[4];
    GLboolean mask[4];
    es.GetIntegerv(GL_SCISSOR_BOX, box);
    es.GetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    es.GetBooleanv(GL_COLOR_WRITEMASK, mask);
    es.Enable(GL_SCISSOR_TEST);
    es.Scissor(x, S.surf_h - y - h, w, h);
    es.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    es.ClearColor(r, g, b, a);
    es.Clear(GL_COLOR_BUFFER_BIT);
    es.ClearColor(cc[0], cc[1], cc[2], cc[3]);
    es.ColorMask(mask[0], mask[1], mask[2], mask[3]);
    es.Scissor(box[0], box[1], box[2], box[3]);
    if (!sc) es.Disable(GL_SCISSOR_TEST);
}
