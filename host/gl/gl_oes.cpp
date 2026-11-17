/*
 * Host GLES1 OES forwarding for the legacy com.google.android.gles_jni.GLImpl JNI.
 *
 * Android's GLES1 wrapper exports these extension entry points directly.  The
 * host Mesa GLES1 library only exports the core API, so resolve the extensions
 * through eglGetProcAddress at first use.  The *Bounds helpers are Android's
 * bounds-checking wrappers and forward to the corresponding GLES1 call.
 */

#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

extern "C" {

GL_API void GL_APIENTRY glColorPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glColorPointer(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glNormalPointerBounds(GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glNormalPointer(type, stride, pointer);
}

GL_API void GL_APIENTRY glTexCoordPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glTexCoordPointer(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glVertexPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glVertexPointer(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glPointSizePointerOESBounds(GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glPointSizePointerOES(type, stride, pointer);
}

GL_API void GL_APIENTRY glMatrixIndexPointerOESBounds(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glMatrixIndexPointerOES(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glWeightPointerOESBounds(GLint size, GLenum type, GLsizei stride, const GLvoid* pointer, GLsizei) {
    glWeightPointerOES(size, type, stride, pointer);
}

GL_API void GL_APIENTRY glBindFramebufferOES(GLenum target, GLuint framebuffer) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLuint framebuffer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glBindFramebufferOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, framebuffer);
}

GL_API void GL_APIENTRY glBindRenderbufferOES(GLenum target, GLuint renderbuffer) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLuint renderbuffer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glBindRenderbufferOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, renderbuffer);
}

GL_API void GL_APIENTRY glBlendEquationOES(GLenum mode) {
    using Fn = void (GL_APIENTRY*)(GLenum mode);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glBlendEquationOES"));
    if (fn == nullptr) {
        return;
    }
    fn(mode);
}

GL_API void GL_APIENTRY glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha) {
    using Fn = void (GL_APIENTRY*)(GLenum modeRGB, GLenum modeAlpha);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glBlendEquationSeparateOES"));
    if (fn == nullptr) {
        return;
    }
    fn(modeRGB, modeAlpha);
}

GL_API void GL_APIENTRY glBlendFuncSeparateOES(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    using Fn = void (GL_APIENTRY*)(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glBlendFuncSeparateOES"));
    if (fn == nullptr) {
        return;
    }
    fn(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GL_API GLenum GL_APIENTRY glCheckFramebufferStatusOES(GLenum target) {
    using Fn = GLenum (GL_APIENTRY*)(GLenum target);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glCheckFramebufferStatusOES"));
    if (fn == nullptr) {
        return 0;
    }
    return fn(target);
}

GL_API void GL_APIENTRY glCurrentPaletteMatrixOES(GLuint matrixpaletteindex) {
    using Fn = void (GL_APIENTRY*)(GLuint matrixpaletteindex);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glCurrentPaletteMatrixOES"));
    if (fn == nullptr) {
        return;
    }
    fn(matrixpaletteindex);
}

GL_API void GL_APIENTRY glDeleteFramebuffersOES(GLsizei n, const GLuint *framebuffers) {
    using Fn = void (GL_APIENTRY*)(GLsizei n, const GLuint *framebuffers);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDeleteFramebuffersOES"));
    if (fn == nullptr) {
        return;
    }
    fn(n, framebuffers);
}

GL_API void GL_APIENTRY glDeleteRenderbuffersOES(GLsizei n, const GLuint *renderbuffers) {
    using Fn = void (GL_APIENTRY*)(GLsizei n, const GLuint *renderbuffers);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDeleteRenderbuffersOES"));
    if (fn == nullptr) {
        return;
    }
    fn(n, renderbuffers);
}

GL_API void GL_APIENTRY glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height) {
    using Fn = void (GL_APIENTRY*)(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexfOES"));
    if (fn == nullptr) {
        return;
    }
    fn(x, y, z, width, height);
}

GL_API void GL_APIENTRY glDrawTexfvOES(const GLfloat *coords) {
    using Fn = void (GL_APIENTRY*)(const GLfloat *coords);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexfvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coords);
}

GL_API void GL_APIENTRY glDrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height) {
    using Fn = void (GL_APIENTRY*)(GLint x, GLint y, GLint z, GLint width, GLint height);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexiOES"));
    if (fn == nullptr) {
        return;
    }
    fn(x, y, z, width, height);
}

GL_API void GL_APIENTRY glDrawTexivOES(const GLint *coords) {
    using Fn = void (GL_APIENTRY*)(const GLint *coords);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexivOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coords);
}

GL_API void GL_APIENTRY glDrawTexsOES(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height) {
    using Fn = void (GL_APIENTRY*)(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexsOES"));
    if (fn == nullptr) {
        return;
    }
    fn(x, y, z, width, height);
}

GL_API void GL_APIENTRY glDrawTexsvOES(const GLshort *coords) {
    using Fn = void (GL_APIENTRY*)(const GLshort *coords);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexsvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coords);
}

GL_API void GL_APIENTRY glDrawTexxOES(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height) {
    using Fn = void (GL_APIENTRY*)(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexxOES"));
    if (fn == nullptr) {
        return;
    }
    fn(x, y, z, width, height);
}

GL_API void GL_APIENTRY glDrawTexxvOES(const GLfixed *coords) {
    using Fn = void (GL_APIENTRY*)(const GLfixed *coords);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glDrawTexxvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coords);
}

GL_API void GL_APIENTRY glFramebufferRenderbufferOES(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glFramebufferRenderbufferOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, attachment, renderbuffertarget, renderbuffer);
}

GL_API void GL_APIENTRY glFramebufferTexture2DOES(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glFramebufferTexture2DOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, attachment, textarget, texture, level);
}

GL_API void GL_APIENTRY glGenFramebuffersOES(GLsizei n, GLuint *framebuffers) {
    using Fn = void (GL_APIENTRY*)(GLsizei n, GLuint *framebuffers);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGenFramebuffersOES"));
    if (fn == nullptr) {
        return;
    }
    fn(n, framebuffers);
}

GL_API void GL_APIENTRY glGenRenderbuffersOES(GLsizei n, GLuint *renderbuffers) {
    using Fn = void (GL_APIENTRY*)(GLsizei n, GLuint *renderbuffers);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGenRenderbuffersOES"));
    if (fn == nullptr) {
        return;
    }
    fn(n, renderbuffers);
}

GL_API void GL_APIENTRY glGenerateMipmapOES(GLenum target) {
    using Fn = void (GL_APIENTRY*)(GLenum target);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGenerateMipmapOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target);
}

GL_API void GL_APIENTRY glGetFramebufferAttachmentParameterivOES(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLenum attachment, GLenum pname, GLint *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGetFramebufferAttachmentParameterivOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, attachment, pname, params);
}

GL_API void GL_APIENTRY glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint *params) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLenum pname, GLint *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGetRenderbufferParameterivOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, pname, params);
}

GL_API void GL_APIENTRY glGetTexGenfvOES(GLenum coord, GLenum pname, GLfloat *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLfloat *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGetTexGenfvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API void GL_APIENTRY glGetTexGenivOES(GLenum coord, GLenum pname, GLint *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLint *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGetTexGenivOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API void GL_APIENTRY glGetTexGenxvOES(GLenum coord, GLenum pname, GLfixed *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLfixed *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glGetTexGenxvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API GLboolean GL_APIENTRY glIsFramebufferOES(GLuint framebuffer) {
    using Fn = GLboolean (GL_APIENTRY*)(GLuint framebuffer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glIsFramebufferOES"));
    if (fn == nullptr) {
        return GL_FALSE;
    }
    return fn(framebuffer);
}

GL_API GLboolean GL_APIENTRY glIsRenderbufferOES(GLuint renderbuffer) {
    using Fn = GLboolean (GL_APIENTRY*)(GLuint renderbuffer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glIsRenderbufferOES"));
    if (fn == nullptr) {
        return GL_FALSE;
    }
    return fn(renderbuffer);
}

GL_API void GL_APIENTRY glLoadPaletteFromModelViewMatrixOES(void) {
    using Fn = void (GL_APIENTRY*)();
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glLoadPaletteFromModelViewMatrixOES"));
    if (fn == nullptr) {
        return;
    }
    fn();
}

GL_API void GL_APIENTRY glMatrixIndexPointerOES(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    using Fn = void (GL_APIENTRY*)(GLint size, GLenum type, GLsizei stride, const void *pointer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glMatrixIndexPointerOES"));
    if (fn == nullptr) {
        return;
    }
    fn(size, type, stride, pointer);
}

GL_API GLbitfield GL_APIENTRY glQueryMatrixxOES(GLfixed *mantissa, GLint *exponent) {
    using Fn = GLbitfield (GL_APIENTRY*)(GLfixed *mantissa, GLint *exponent);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glQueryMatrixxOES"));
    if (fn == nullptr) {
        return 0;
    }
    return fn(mantissa, exponent);
}

GL_API void GL_APIENTRY glRenderbufferStorageOES(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    using Fn = void (GL_APIENTRY*)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glRenderbufferStorageOES"));
    if (fn == nullptr) {
        return;
    }
    fn(target, internalformat, width, height);
}

GL_API void GL_APIENTRY glTexGenfOES(GLenum coord, GLenum pname, GLfloat param) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLfloat param);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGenfOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, param);
}

GL_API void GL_APIENTRY glTexGenfvOES(GLenum coord, GLenum pname, const GLfloat *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, const GLfloat *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGenfvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API void GL_APIENTRY glTexGeniOES(GLenum coord, GLenum pname, GLint param) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLint param);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGeniOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, param);
}

GL_API void GL_APIENTRY glTexGenivOES(GLenum coord, GLenum pname, const GLint *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, const GLint *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGenivOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API void GL_APIENTRY glTexGenxOES(GLenum coord, GLenum pname, GLfixed param) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, GLfixed param);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGenxOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, param);
}

GL_API void GL_APIENTRY glTexGenxvOES(GLenum coord, GLenum pname, const GLfixed *params) {
    using Fn = void (GL_APIENTRY*)(GLenum coord, GLenum pname, const GLfixed *params);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glTexGenxvOES"));
    if (fn == nullptr) {
        return;
    }
    fn(coord, pname, params);
}

GL_API void GL_APIENTRY glWeightPointerOES(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    using Fn = void (GL_APIENTRY*)(GLint size, GLenum type, GLsizei stride, const void *pointer);
    static Fn fn = reinterpret_cast<Fn>(eglGetProcAddress("glWeightPointerOES"));
    if (fn == nullptr) {
        return;
    }
    fn(size, type, stride, pointer);
}

}  // extern "C"
