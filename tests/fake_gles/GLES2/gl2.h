#ifndef TEST_FAKE_GLES2_GL2_H
#define TEST_FAKE_GLES2_GL2_H

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float GLfloat;

#define GL_NO_ERROR 0u
#define GL_INVALID_OPERATION 0x0502u
#define GL_FRAMEBUFFER 0x8d40u
#define GL_SCISSOR_TEST 0x0c11u
#define GL_COLOR_BUFFER_BIT 0x00004000u
#define GL_TRUE 1u

GLenum glGetError(void);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glDisable(GLenum capability);
void glColorMask(GLboolean red, GLboolean green,
                 GLboolean blue, GLboolean alpha);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glClear(GLbitfield mask);
void glFinish(void);

#endif
