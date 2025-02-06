
#ifndef gl_h
#define gl_h

#define glversion 2

#ifdef __APPLE__
    #include <OpenGLES/ES2/gl.h>
    #include <OpenGLES/ES2/glext.h>
#elif defined(__ANDROID__)
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
    #define GL_BGRA GL_BGRA_EXT
#else
    #pragma error "windows does not supported yet"
#endif

#if 0
    #define glcheck(x) \
    do { \
        logmsg(#x"\n"); \
        x; \
        GLenum glerr = glGetError(); \
        if (glerr != GL_NO_ERROR) { \
            logmsg("opengl error: %d, file: %s, line: %d\n", glerr, __FILE__, __LINE__); \
        } \
    } while(0)
#else
    #define glcheck(x) x
#endif

#if defined(__APPLE__)
    #define glGenVertexArrays glGenVertexArraysOES
    #define glBindVertexArray glBindVertexArrayOES
    #define glDeleteVertexArrays glDeleteVertexArraysOES
#elif defined(__ANDROID__)
    // if no one else use opengl cocurrently, it is safe
    // to simply comment glGenVertexArrays things which are
    // not supported by android before 4.3 (opengl es 3.0)
    // because my vertex always is currently selected by opengl
    // -- e.g. the switching to it is not necessary.
    #if (glversion < 3)
        #define glGenVertexArrays(x, y) (void) 0
        #define glBindVertexArray(x) (void) 0
        #define glDeleteVertexArrays(x, y) (void) 0
    #else
        #define glGenVertexArrays glGenVertexArraysOES
        #define glBindVertexArray glBindVertexArrayOES
        #define glDeleteVertexArrays glDeleteVertexArraysOES
    #endif
#endif

#define nr_textures 6
#endif
