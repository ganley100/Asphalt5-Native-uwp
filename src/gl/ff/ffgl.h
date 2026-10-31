/* Desktop OpenGL 1.x surface for the UWP / Xbox build.
 *
 * The GLES1 layer in src/gl/ was written against desktop opengl32 (WGL).
 * UWP has no OpenGL, so this header declares the same entry points and
 * ff_gles.c implements them on OpenGL ES 2/3 (ANGLE on Direct3D 11) with
 * generated shaders that reproduce the fixed-function pipeline.
 *
 * Only the subset the host actually calls is provided.
 */
#ifndef ASPHALT5_FFGL_H
#define ASPHALT5_FFGL_H

#include <stddef.h>
#include <stdint.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef int GLfixed;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW 0x0503
#define GL_STACK_UNDERFLOW 0x0504

/* primitives */
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007

/* types */
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_FIXED 0x140C
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_SHORT_5_6_5 0x8363

/* buffers */
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ALL_ATTRIB_BITS 0x000fffff
#define GL_CLIENT_ALL_ATTRIB_BITS 0xffffffff
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901

/* capabilities */
#define GL_POINT_SMOOTH 0x0B10
#define GL_LINE_SMOOTH 0x0B20
#define GL_CULL_FACE 0x0B44
#define GL_LIGHTING 0x0B50
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_COLOR_MATERIAL 0x0B57
#define GL_FOG 0x0B60
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_NORMALIZE 0x0BA1
#define GL_ALPHA_TEST 0x0BC0
#define GL_DITHER 0x0BD0
#define GL_BLEND 0x0BE2
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_2D 0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_RESCALE_NORMAL 0x803A
#define GL_MULTISAMPLE 0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_ALPHA_TO_ONE 0x809F
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_CLIP_PLANE0 0x3000
#define GL_LIGHT0 0x4000
#define GL_POINT_SPRITE_OES 0x8861
#define GL_MATRIX_PALETTE_OES 0x8840

/* client arrays */
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_MATRIX_INDEX_ARRAY_OES 0x8844
#define GL_WEIGHT_ARRAY_OES 0x86AD
#define GL_POINT_SIZE_ARRAY_OES 0x8B9C

/* queries */
#define GL_CURRENT_COLOR 0x0B00
#define GL_CURRENT_NORMAL 0x0B02
#define GL_CURRENT_TEXTURE_COORDS 0x0B03
#define GL_CULL_FACE_MODE 0x0B45
#define GL_FRONT_FACE 0x0B46
#define GL_SHADE_MODEL 0x0B54
#define GL_DEPTH_RANGE 0x0B70
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#define GL_DEPTH_FUNC 0x0B74
#define GL_ALPHA_TEST_FUNC 0x0BC1
#define GL_ALPHA_TEST_REF 0x0BC2
#define GL_MATRIX_MODE 0x0BA0
#define GL_VIEWPORT 0x0BA2
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#define GL_TEXTURE_STACK_DEPTH 0x0BA5
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_TEXTURE_MATRIX 0x0BA8
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_MAX_LIGHTS 0x0D31
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0D39
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_RED_BITS 0x0D52
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_MAX_TEXTURE_UNITS 0x84E2
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003

/* strings */
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03

/* functions */
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308

/* shading / matrices */
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702

/* lights and materials */
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_POSITION 0x1203
#define GL_SPOT_DIRECTION 0x1204
#define GL_SPOT_EXPONENT 0x1205
#define GL_SPOT_CUTOFF 0x1206
#define GL_CONSTANT_ATTENUATION 0x1207
#define GL_LINEAR_ATTENUATION 0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#define GL_EMISSION 0x1600
#define GL_SHININESS 0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602

/* fog */
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66

/* pixel formats */
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_BGRA 0x80E1
#define GL_BGRA_EXT 0x80E1

/* textures */
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP 0x2900
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_GENERATE_MIPMAP 0x8191
#define GL_GENERATE_MIPMAP_HINT 0x8192
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1

/* texture environment */
#define GL_ADD 0x0104
#define GL_REPLACE 0x1E01
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_TEXTURE_ENV 0x2300
#define GL_SUBTRACT 0x84E7
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_RGB_SCALE 0x8573
#define GL_ADD_SIGNED 0x8574
#define GL_INTERPOLATE 0x8575
#define GL_CONSTANT 0x8576
#define GL_PRIMARY_COLOR 0x8577
#define GL_PREVIOUS 0x8578
#define GL_SRC0_RGB 0x8580
#define GL_SRC1_RGB 0x8581
#define GL_SRC2_RGB 0x8582
#define GL_SRC0_ALPHA 0x8588
#define GL_SRC1_ALPHA 0x8589
#define GL_SRC2_ALPHA 0x858A
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_OPERAND2_RGB 0x8592
#define GL_OPERAND0_ALPHA 0x8598
#define GL_OPERAND1_ALPHA 0x8599
#define GL_OPERAND2_ALPHA 0x859A
#define GL_ALPHA_SCALE 0x0D1C
#define GL_DOT3_RGB 0x86AE
#define GL_DOT3_RGBA 0x86AF

#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_FOG_HINT 0x0C54
#define GL_DONT_CARE 0x1100
#define GL_FASTEST 0x1101
#define GL_NICEST 0x1102

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the desktop GL subset implemented by ff_gles.c ------------------- */
void glAlphaFunc(GLenum func, GLclampf ref);
void glBegin(GLenum mode);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFunc(GLenum s, GLenum d);
void glClear(GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepth(GLclampd d);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glCopyTexImage2D(GLenum target, GLint level, GLenum ifmt, GLint x, GLint y,
                      GLsizei w, GLsizei h, GLint border);
void glCullFace(GLenum mode);
void glDeleteTextures(GLsizei n, const GLuint *t);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDisable(GLenum cap);
void glDisableClientState(GLenum array);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glEnable(GLenum cap);
void glEnableClientState(GLenum array);
void glEnd(void);
void glFinish(void);
void glFogf(GLenum pname, GLfloat v);
void glFogfv(GLenum pname, const GLfloat *v);
void glFrontFace(GLenum mode);
void glGenTextures(GLsizei n, GLuint *t);
void glGetBooleanv(GLenum pname, GLboolean *v);
GLenum glGetError(void);
void glGetFloatv(GLenum pname, GLfloat *v);
void glGetIntegerv(GLenum pname, GLint *v);
void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *v);
const GLubyte *glGetString(GLenum name);
void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *v);
void glGetTexEnviv(GLenum target, GLenum pname, GLint *v);
void glGetTexImage(GLenum target, GLint level, GLenum fmt, GLenum type, void *px);
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *v);
void glHint(GLenum target, GLenum mode);
GLboolean glIsEnabled(GLenum cap);
void glLightModelfv(GLenum pname, const GLfloat *v);
void glLightf(GLenum light, GLenum pname, GLfloat v);
void glLightfv(GLenum light, GLenum pname, const GLfloat *v);
void glLoadIdentity(void);
void glLoadMatrixf(const GLfloat *m);
void glMaterialf(GLenum face, GLenum pname, GLfloat v);
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *v);
void glMatrixMode(GLenum mode);
void glNormalPointer(GLenum type, GLsizei stride, const void *p);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glPixelStorei(GLenum pname, GLint v);
void glPopAttrib(void);
void glPopClientAttrib(void);
void glPopMatrix(void);
void glPushAttrib(GLbitfield mask);
void glPushClientAttrib(GLbitfield mask);
void glPushMatrix(void);
void glReadBuffer(GLenum mode);
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *px);
void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glShadeModel(GLenum mode);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *v);
void glTexEnvi(GLenum target, GLenum pname, GLint v);
void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                  GLint border, GLenum fmt, GLenum type, const void *px);
void glTexParameterf(GLenum target, GLenum pname, GLfloat v);
void glTexParameteri(GLenum target, GLenum pname, GLint v);
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                     GLsizei h, GLenum fmt, GLenum type, const void *px);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glVertex2i(GLint x, GLint y);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);

/* Multitexture / buffer entry points (the desktop build loads these with
 * wglGetProcAddress; here they are plain functions). */
void APIENTRY ff_ActiveTexture(GLenum unit);
void APIENTRY ff_ClientActiveTexture(GLenum unit);
void APIENTRY ff_BindBuffer(GLenum target, GLuint buffer);
void APIENTRY ff_CompressedTexImage2D(GLenum target, GLint level, GLenum fmt,
                                      GLsizei w, GLsizei h, GLint border,
                                      GLsizei size, const void *data);

/* ---- host hooks -------------------------------------------------------- */
/* Resolve the OpenGL ES entry points (returns 0 on success). */
int ff_init(void *(*getproc)(const char *name));
/* Size of the default framebuffer, used by overlays. */
void ff_set_surface_size(int w, int h);
/* Solid rectangle in surface pixels (top-left origin), drawn on top of the
 * current frame without disturbing the emulated GL state. */
void ff_overlay_rect(int x, int y, int w, int h, float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif

#endif /* ASPHALT5_FFGL_H */
