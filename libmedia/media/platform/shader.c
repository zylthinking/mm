
#include "shader.h"
#include "mem.h"
#include "math.h"

typedef struct {
    GLuint vsh;
    GLuint fsh;
    GLuint prog;
    GLuint vertex_index, texture_index, matrix_index, ratio_index1, ratio_index2;
#if !defined(__APPLE__)
    GLuint mask_index[2];
#endif
    GLuint tex_y_indx, tex_u_indx, tex_v_indx;
    GLuint shader_video;
    GLfloat shader_video_type;
    GLfloat ratio[2];
    GLfloat vec4[2][5];
} shader_t;

static GLchar* fsh_code = "#version 100\n"
                          "precision highp float;"
                          "varying vec4 texture_coord;"
#if !defined(__APPLE__)
                          "varying vec4 vertex_coord;"
                          "uniform vec4 mask0;"
                          "uniform vec4 mask1;"
#endif
                          "uniform float video_type;"
                          "uniform sampler2D tex_y;"
                          "uniform sampler2D tex_u;"
                          "uniform sampler2D tex_v;"
                          "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
                          "void main() {"
#if !defined(__APPLE__)
                          "    if (vertex_coord.z == 0.0) {"
                          "        float x = vertex_coord.x;"
                          "        float y = vertex_coord.y;"
                          "        x = mask0.x + x * (mask0.z - mask0.x);"
                          "        y = mask0.y + y * (mask0.w - mask0.y);"
                          "        if (x > mask1.x && x < mask1.z && y > mask1.y && y < mask1.w) {"
                          "            discard;"
                          "        }"
                          "    }"
#endif
                          "    if (video_type == 0.0) {"
                          "        gl_FragColor = texture2D(tex_y, texture_coord.st);"
                          "    } else if (video_type == 1.0) { "
                          "        vec3 yuv;"
                          "        vec3 rgb;"
                          "        yuv.x = texture2D(tex_y, texture_coord.st).r;"
                          "        yuv.y = texture2D(tex_u, texture_coord.st).r;"
                          "        yuv.z = texture2D(tex_v, texture_coord.st).r;"
                          "        yuv += offset;"
                          "        rgb = mat3(1.1643,  1.1643,    1.1643,"
                          "                   0,       -0.39173,  2.017,"
                          "                   1.5958,  -0.81290,  0) * yuv;"
                          "        gl_FragColor = vec4(rgb, 1);"
                          "    } else if (video_type == 2.0) {"
                          "        vec3 yuv;"
                          "        vec3 rgb;"
                          "        yuv.x = texture2D(tex_y, texture_coord.st).r;"
                          "        yuv.y = texture2D(tex_u, texture_coord.st).r;"
                          "        yuv.z = texture2D(tex_u, texture_coord.st).a;"
                          "        yuv += offset;"
                          "        rgb = mat3(1.1643,  1.1643,    1.1643,"
                          "                   0,       -0.39173,  2.017,"
                          "                   1.5958,  -0.81290,  0) * yuv;"
                          "        gl_FragColor = vec4(rgb, 1);"
                          "    } else if (video_type == 3.0) {"
                          "        vec3 rgb;"
                          "        rgb.g = texture2D(tex_y, texture_coord.st).r;"
                          "        rgb.b = texture2D(tex_u, texture_coord.st).r;"
                          "        rgb.r = texture2D(tex_v, texture_coord.st).r;"
                          "        gl_FragColor = vec4(rgb, 1);"
                          "    } else if (video_type == 4.0) {"
                          "        vec3 yuv;"
                          "        vec3 rgb;"
                          "        yuv.x = texture2D(tex_y, texture_coord.st).r;"
                          "        yuv.y = texture2D(tex_u, texture_coord.st).a;"
                          "        yuv.z = texture2D(tex_u, texture_coord.st).r;"
                          "        yuv += offset;"
                          "        rgb = mat3(1.1643,  1.1643,    1.1643,"
                          "                   0,       -0.39173,  2.017,"
                          "                   1.5958,  -0.81290,  0) * yuv;"
                          "        gl_FragColor = vec4(rgb, 1);"
                          "    }"
                          "}";

static GLchar* vsh_code = "#version 100\n"
                          "attribute vec4 texture;"
                          "attribute vec4 vertex;"
                          "uniform float ratio1, ratio2;"
                          "uniform mat4 matrix;"
                          "varying vec4 texture_coord;"
                          "varying vec4 vertex_coord;"
                          "void main() {"
                          "    gl_Position = matrix * vertex;"
                          "    texture_coord.s = texture.s * ratio1;"
                          "    texture_coord.t = texture.t * ratio2;"
                          "    vertex_coord = vertex;"
                          "}";


static void print_err(GLuint name, int compile)
{
    GLchar buf[1024];
    GLint length;
    if (compile) {
        glGetShaderInfoLog(name, 1024, &length, buf);
    } else {
        glGetProgramInfoLog(name, 1024, &length, buf);
    }
    logmsg("%s\n", buf);
}

static GLuint shader_create(const GLchar* code, GLenum type)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, &code, NULL);
    glCompileShader(shader);

    GLint ret;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ret);
    if (GL_TRUE != ret) {
        print_err(shader, 1);
        glDeleteShader(shader);
        shader = 0;
    }
    return shader;
}

void* shader_create_objects()
{
    shader_t* shader = (shader_t *) my_malloc(sizeof(shader_t));
    if (shader == NULL) {
        return NULL;
    }
    shader->vec4[0][0] = shader->vec4[0][1] = 0.0;
    shader->vec4[0][2] = shader->vec4[0][3] = shader->vec4[0][4] = 0.0;
    shader->vec4[1][0] = shader->vec4[1][1] = 0.0;
    shader->vec4[1][2] = shader->vec4[1][3] = shader->vec4[1][4] = 0.0;

    shader->vsh = shader_create(vsh_code, GL_VERTEX_SHADER);
    if (shader->vsh == 0) {
        goto LABEL4;
    }

    shader->fsh = shader_create(fsh_code, GL_FRAGMENT_SHADER);
    if (shader->fsh == 0) {
        goto LABEL3;
    }

    shader->prog = glCreateProgram();
    if (shader->prog == 0) {
        goto LABEL2;
    }

    glAttachShader(shader->prog, shader->vsh);
    glAttachShader(shader->prog, shader->fsh);
    glLinkProgram(shader->prog);
    GLint ret;
    glGetProgramiv(shader->prog, GL_LINK_STATUS, &ret);
    if (GL_TRUE != ret) {
        print_err(shader->prog, 0);
        goto LABEL1;
    }

    glValidateProgram(shader->prog);
    glGetProgramiv(shader->prog, GL_VALIDATE_STATUS, &ret);
    if (GL_TRUE != ret) {
        goto LABEL1;
    }

    shader->vertex_index = glGetAttribLocation(shader->prog, "vertex");
    shader->matrix_index = glGetUniformLocation(shader->prog, "matrix");
    shader->texture_index = glGetAttribLocation(shader->prog, "texture");
    shader->tex_y_indx = glGetUniformLocation(shader->prog, "tex_y");
    shader->tex_u_indx = glGetUniformLocation(shader->prog, "tex_u");
    shader->tex_v_indx = glGetUniformLocation(shader->prog, "tex_v");
    shader->shader_video = glGetUniformLocation(shader->prog, "video_type");
#if !defined(__APPLE__)
    shader->mask_index[0] = glGetUniformLocation(shader->prog, "mask0");
    shader->mask_index[1] = glGetUniformLocation(shader->prog, "mask1");
#endif
    shader->shader_video_type = 0.0;
    shader->ratio_index1 = glGetUniformLocation(shader->prog, "ratio1");
    shader->ratio_index2 = glGetUniformLocation(shader->prog, "ratio2");
    shader->ratio[0] = shader->ratio[1] = 0.0;

    glUseProgram(shader->prog);
    return shader;

LABEL1:
    glDetachShader(shader->prog, shader->vsh);
    glDetachShader(shader->prog, shader->fsh);
    glDeleteProgram(shader->prog);
LABEL2:
    glDeleteShader(shader->fsh);
LABEL3:
    glDeleteShader(shader->vsh);
LABEL4:
    my_free(shader);
    return NULL;
}

void shader_delete_objects(void* ptr)
{
    shader_t* shader = (shader_t *) ptr;
    glDetachShader(shader->prog, shader->vsh);
    glDetachShader(shader->prog, shader->fsh);
    glDeleteProgram(shader->prog);
    glDeleteShader(shader->fsh);
    glDeleteShader(shader->vsh);
    shader->fsh = shader->vsh = shader->prog = 0;
    shader->shader_video_type = 0.0;
    my_free(shader);
}

void shader_update_vertex(void* ptr, int z, int first, int angle)
{
    shader_t* shader = (shader_t *) ptr;
    size_t offset = sizeof(GLfloat) * 12 * z;
    size_t offset2 = 0;
    if (angle & 0x80) {
        offset2 = sizeof(GLfloat) * 96;
        angle &= 0x0f;
    }
    size_t offset3 = sizeof(GLfloat) * 24 * angle;

    glVertexAttribPointer(shader->vertex_index, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid *) (offset + offset2 + offset3));

    if (first) {
        glEnableVertexAttribArray(shader->vertex_index);
    }
}

intptr_t shader_change_mask(void* ptr, intptr_t idx, float fx, float fy, float fw, float fh, float fz)
{
#if !defined(__APPLE__)
    shader_t* shader = (shader_t *) ptr;
    fw += fx;
    fh += fy;

    if (shader->vec4[0][0] == fx &&
        shader->vec4[0][1] == fy &&
        shader->vec4[0][2] == fw &&
        shader->vec4[0][3] == fh &&
        shader->vec4[0][4] == fz) {
        return shader->mask_index[0];
    }

    if (shader->vec4[1][0] == fx &&
        shader->vec4[1][1] == fy &&
        shader->vec4[1][2] == fw &&
        shader->vec4[1][3] == fh &&
        shader->vec4[1][4] == fz) {
        return shader->mask_index[1];
    }

    intptr_t old = -1, new;
    if (fz == 0.0) {
        if (idx == shader->mask_index[1]) {
            old = 1;
        }
        new = 0;
    } else {
        if (idx == shader->mask_index[0]) {
            old = 0;
        }
        new = 1;
    }

    if (old != -1) {
        shader->vec4[old][0] = shader->vec4[old][1] = 0.0;
        shader->vec4[old][2] = shader->vec4[old][3] = shader->vec4[old][4] = 0.0;
        glUniform4fv(shader->mask_index[old], 1, (const GLfloat *) &shader->vec4[old][0]);
    }

    shader->vec4[new][0] = fx;
    shader->vec4[new][1] = fy;
    shader->vec4[new][2] = fw;
    shader->vec4[new][3] = fh;
    shader->vec4[new][4] = fz;
    idx = shader->mask_index[new];
    glUniform4fv(idx, 1, (const GLfloat *) &shader->vec4[new][0]);
#endif
    return idx;
}

intptr_t shader_clear_mask(void* ptr)
{
#if !defined(__APPLE__)
      shader_t* shader = (shader_t *) ptr;
      shader->vec4[1][0] = shader->vec4[1][1] = shader->vec4[1][2] = shader->vec4[1][3] = shader->vec4[1][4] = 0.0;
      glUniform4fv(shader->mask_index[1], 1, (const GLfloat *) &shader->vec4[1][0]);
#endif
      return -1;
}

void shader_texture_ratio(void* ptr, float width, float height)
{
    shader_t* shader = (shader_t *) ptr;
    if (shader->ratio[0] != width) {
        shader->ratio[0] = width;
        glUniform1f(shader->ratio_index1, (GLfloat) width);
    }

    if (shader->ratio[1] != height) {
        shader->ratio[1] = height;
        glUniform1f(shader->ratio_index2, (GLfloat) height);
    }
}

static GLfloat* _math_matrix_ortho(GLfloat m[16], GLfloat left, GLfloat right,
                                   GLfloat bottom, GLfloat top,
                                   GLfloat nearval, GLfloat farval)
{
#define M(row,col)  m[col*4+row]
    M(0,0) = 2.0F / (right-left);
    M(0,1) = 0.0F;
    M(0,2) = 0.0F;
    M(0,3) = -(right+left) / (right-left);

    M(1,0) = 0.0F;
    M(1,1) = 2.0F / (top-bottom);
    M(1,2) = 0.0F;
    M(1,3) = -(top+bottom) / (top-bottom);

    M(2,0) = 0.0F;
    M(2,1) = 0.0F;
    M(2,2) = -2.0F / (farval-nearval);
    M(2,3) = -(farval+nearval) / (farval-nearval);

    M(3,0) = 0.0F;
    M(3,1) = 0.0F;
    M(3,2) = 0.0F;
    M(3,3) = 1.0F;
#undef M
    return &m[0];
}

void shader_ortho(void* ptr, GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearval, GLfloat farval)
{
    GLfloat m[16];
    GLfloat* matrix = _math_matrix_ortho(m, left, right, bottom, top, nearval, farval);
    shader_t* shader = (shader_t *) ptr;
    glUniformMatrix4fv(shader->matrix_index, 1, GL_FALSE, matrix);
}

void shader_update_texture(void* ptr)
{
    shader_t* shader = (shader_t *) ptr;
    glVertexAttribPointer(shader->texture_index, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *) (NULL));
    glEnableVertexAttribArray(shader->texture_index);
    glUniform1i(shader->tex_y_indx, 0);
    glUniform1i(shader->tex_u_indx, 1);
    glUniform1i(shader->tex_v_indx, 2);
}

void shader_set_type(void* ptr, int type)
{
    shader_t* shader = (shader_t *) ptr;
    if (shader->shader_video_type == (GLfloat) type) {
        return;
    }
    shader->shader_video_type = (GLfloat) type;
    glUniform1f(shader->shader_video, shader->shader_video_type);
}
