#ifndef BS_RENDER_GL_H
#define BS_RENDER_GL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The OpenGL 3.3 core subset the renderer uses, loaded at run time through
 * a platform callback (e.g. SDL_GL_GetProcAddress). Only what we call is
 * declared, which keeps this self-contained on every platform.
 */

#if defined(_WIN32) && !defined(__CYGWIN__)
#define BS_GLAPI __stdcall
#else
#define BS_GLAPI
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_INVALID_INDEX 0xFFFFFFFFu

/* X(return type, name, parameter list) */
#define BS_GL_FUNCTIONS(X)                                                                          \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                   \
    X(void, glClear, (GLbitfield mask))                                                             \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                             \
    X(void, glEnable, (GLenum cap))                                                                 \
    X(void, glDisable, (GLenum cap))                                                                \
    X(void, glCullFace, (GLenum mode))                                                              \
    X(void, glFrontFace, (GLenum mode))                                                             \
    X(void, glDepthFunc, (GLenum func))                                                             \
    X(void, glDepthMask, (GLboolean flag))                                                          \
    X(void, glBlendFunc, (GLenum s, GLenum d))                                                      \
    X(GLenum, glGetError, (void))                                                                   \
    X(const GLubyte *, glGetString, (GLenum name))                                                  \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                             \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p))    \
    X(void, glGenTextures, (GLsizei n, GLuint *t))                                                  \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *t))                                         \
    X(void, glBindTexture, (GLenum target, GLuint t))                                               \
    X(void, glActiveTexture, (GLenum unit))                                                         \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, \
                           GLenum fmt, GLenum type, const void *data))                              \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                            \
    X(void, glGenerateMipmap, (GLenum target))                                                      \
    X(GLuint, glCreateShader, (GLenum type))                                                        \
    X(void, glShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len))      \
    X(void, glCompileShader, (GLuint s))                                                            \
    X(void, glGetShaderiv, (GLuint s, GLenum pname, GLint *v))                                      \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei cap, GLsizei *len, GLchar *log))                 \
    X(void, glDeleteShader, (GLuint s))                                                             \
    X(GLuint, glCreateProgram, (void))                                                              \
    X(void, glAttachShader, (GLuint p, GLuint s))                                                   \
    X(void, glLinkProgram, (GLuint p))                                                              \
    X(void, glGetProgramiv, (GLuint p, GLenum pname, GLint *v))                                     \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei cap, GLsizei *len, GLchar *log))                \
    X(void, glDeleteProgram, (GLuint p))                                                            \
    X(void, glUseProgram, (GLuint p))                                                               \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar *name))                                  \
    X(GLuint, glGetUniformBlockIndex, (GLuint p, const GLchar *name))                               \
    X(void, glUniformBlockBinding, (GLuint p, GLuint block, GLuint binding))                        \
    X(void, glUniform1i, (GLint loc, GLint v))                                                      \
    X(void, glUniform1f, (GLint loc, GLfloat v))                                                    \
    X(void, glUniform2f, (GLint loc, GLfloat a, GLfloat b))                                         \
    X(void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                              \
    X(void, glUniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                   \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei n, GLboolean transpose, const GLfloat *v))      \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *a))                                              \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *a))                                     \
    X(void, glBindVertexArray, (GLuint a))                                                          \
    X(void, glGenBuffers, (GLsizei n, GLuint *b))                                                   \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *b))                                          \
    X(void, glBindBuffer, (GLenum target, GLuint b))                                                \
    X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint b))                              \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))         \
    X(void, glVertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, \
                                    const void *ptr))                                               \
    X(void, glEnableVertexAttribArray, (GLuint i))                                                  \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *f))                                              \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *f))                                     \
    X(void, glBindFramebuffer, (GLenum target, GLuint f))                                           \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum att, GLenum textarget, GLuint t, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                            \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *r))                                             \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *r))                                    \
    X(void, glBindRenderbuffer, (GLenum target, GLuint r))                                          \
    X(void, glRenderbufferStorage, (GLenum target, GLenum fmt, GLsizei w, GLsizei h))               \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum att, GLenum rbtarget, GLuint r))      \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,   \
                                GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

#define BS_GL_DECLARE(ret, name, params) \
    typedef ret(BS_GLAPI *PFN_##name) params; \
    extern PFN_##name bs_##name;
BS_GL_FUNCTIONS(BS_GL_DECLARE)
#undef BS_GL_DECLARE

/* Call through the loaded pointers with the usual names. */
#define glViewport bs_glViewport
#define glClear bs_glClear
#define glClearColor bs_glClearColor
#define glEnable bs_glEnable
#define glDisable bs_glDisable
#define glCullFace bs_glCullFace
#define glFrontFace bs_glFrontFace
#define glDepthFunc bs_glDepthFunc
#define glDepthMask bs_glDepthMask
#define glBlendFunc bs_glBlendFunc
#define glGetError bs_glGetError
#define glGetString bs_glGetString
#define glPixelStorei bs_glPixelStorei
#define glReadPixels bs_glReadPixels
#define glGenTextures bs_glGenTextures
#define glDeleteTextures bs_glDeleteTextures
#define glBindTexture bs_glBindTexture
#define glActiveTexture bs_glActiveTexture
#define glTexImage2D bs_glTexImage2D
#define glTexParameteri bs_glTexParameteri
#define glGenerateMipmap bs_glGenerateMipmap
#define glCreateShader bs_glCreateShader
#define glShaderSource bs_glShaderSource
#define glCompileShader bs_glCompileShader
#define glGetShaderiv bs_glGetShaderiv
#define glGetShaderInfoLog bs_glGetShaderInfoLog
#define glDeleteShader bs_glDeleteShader
#define glCreateProgram bs_glCreateProgram
#define glAttachShader bs_glAttachShader
#define glLinkProgram bs_glLinkProgram
#define glGetProgramiv bs_glGetProgramiv
#define glGetProgramInfoLog bs_glGetProgramInfoLog
#define glDeleteProgram bs_glDeleteProgram
#define glUseProgram bs_glUseProgram
#define glGetUniformLocation bs_glGetUniformLocation
#define glGetUniformBlockIndex bs_glGetUniformBlockIndex
#define glUniformBlockBinding bs_glUniformBlockBinding
#define glUniform1i bs_glUniform1i
#define glUniform1f bs_glUniform1f
#define glUniform2f bs_glUniform2f
#define glUniform3f bs_glUniform3f
#define glUniform4f bs_glUniform4f
#define glUniformMatrix4fv bs_glUniformMatrix4fv
#define glGenVertexArrays bs_glGenVertexArrays
#define glDeleteVertexArrays bs_glDeleteVertexArrays
#define glBindVertexArray bs_glBindVertexArray
#define glGenBuffers bs_glGenBuffers
#define glDeleteBuffers bs_glDeleteBuffers
#define glBindBuffer bs_glBindBuffer
#define glBindBufferBase bs_glBindBufferBase
#define glBufferData bs_glBufferData
#define glVertexAttribPointer bs_glVertexAttribPointer
#define glEnableVertexAttribArray bs_glEnableVertexAttribArray
#define glDrawArrays bs_glDrawArrays
#define glGenFramebuffers bs_glGenFramebuffers
#define glDeleteFramebuffers bs_glDeleteFramebuffers
#define glBindFramebuffer bs_glBindFramebuffer
#define glFramebufferTexture2D bs_glFramebufferTexture2D
#define glCheckFramebufferStatus bs_glCheckFramebufferStatus
#define glGenRenderbuffers bs_glGenRenderbuffers
#define glDeleteRenderbuffers bs_glDeleteRenderbuffers
#define glBindRenderbuffer bs_glBindRenderbuffer
#define glRenderbufferStorage bs_glRenderbufferStorage
#define glFramebufferRenderbuffer bs_glFramebufferRenderbuffer
#define glBlitFramebuffer bs_glBlitFramebuffer

typedef void (*bs_gl_proc)(void);
typedef bs_gl_proc (*bs_gl_loader)(const char *name);

/* Loads every function; returns false (naming the first missing one) on failure. */
bool bs_gl_load(bs_gl_loader loader, const char **missing);

#endif
