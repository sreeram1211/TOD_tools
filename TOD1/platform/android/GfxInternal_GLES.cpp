/*
 * GfxInternal_GLES.cpp
 *
 * OpenGL ES 3.0 graphics backend implementation.
 * Replaces DirectX 9 renderer for Android.
 */
#include "GfxInternal_GLES.h"
#include "Platform_Android.h"
#include <cstring>
#include <cmath>

GfxInternal_GLES* GfxInternal_GLES::s_Instance = nullptr;

// ===========================================================================
// Built-in shader sources
// ===========================================================================

// 2D shader: position(2f) + color(4ub) + texcoord(2f)
static const char* s_Vertex2D = R"glsl(#version 300 es
    precision mediump float;
    layout(location = 0) in vec2 a_Position;
    layout(location = 1) in vec4 a_Color;
    layout(location = 2) in vec2 a_TexCoord;
    uniform mat4 u_Projection;
    out vec4 v_Color;
    out vec2 v_TexCoord;
    void main() {
        gl_Position = u_Projection * vec4(a_Position, 0.0, 1.0);
        v_Color = a_Color;
        v_TexCoord = a_TexCoord;
    }
)glsl";

static const char* s_Fragment2D = R"glsl(#version 300 es
    precision mediump float;
    in vec4 v_Color;
    in vec2 v_TexCoord;
    uniform sampler2D u_Texture;
    uniform int u_UseTexture;
    out vec4 fragColor;
    void main() {
        if (u_UseTexture != 0)
            fragColor = texture(u_Texture, v_TexCoord) * v_Color;
        else
            fragColor = v_Color;
    }
)glsl";

// 3D shader: position(3f) + normal(3f) + texcoord(2f)
static const char* s_Vertex3D = R"glsl(#version 300 es
    precision mediump float;
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec4 a_Color;
    uniform mat4 u_MVP;
    out vec4 v_Color;
    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
        v_Color = a_Color;
    }
)glsl";

static const char* s_Fragment3D = R"glsl(#version 300 es
    precision mediump float;
    in vec4 v_Color;
    out vec4 fragColor;
    void main() {
        fragColor = v_Color;
    }
)glsl";

// 3D textured shader: position(3f) + normal(3f) + texcoord(2f)
static const char* s_Vertex3DTex = R"glsl(#version 300 es
    precision highp float;
    layout(location = 0) in vec3 a_Position;
    layout(location = 1) in vec3 a_Normal;
    layout(location = 2) in vec2 a_TexCoord;
    uniform mat4 u_MVP;
    uniform mat4 u_Model;
    uniform vec3 u_LightDir;
    out vec2 v_TexCoord;
    out float v_Diffuse;
    void main() {
        gl_Position = u_MVP * vec4(a_Position, 1.0);
        v_TexCoord = a_TexCoord;
        vec3 worldNormal = normalize(mat3(u_Model) * a_Normal);
        v_Diffuse = max(dot(worldNormal, normalize(u_LightDir)), 0.2);
    }
)glsl";

static const char* s_Fragment3DTex = R"glsl(#version 300 es
    precision highp float;
    in vec2 v_TexCoord;
    in float v_Diffuse;
    uniform sampler2D u_Texture;
    out vec4 fragColor;
    void main() {
        vec4 texColor = texture(u_Texture, v_TexCoord);
        fragColor = vec4(texColor.rgb * v_Diffuse, texColor.a);
    }
)glsl";

// ===========================================================================
// Mat4 implementation
// ===========================================================================
Mat4::Mat4() {
    memset(m, 0, sizeof(m));
}

Mat4 Mat4::Identity() {
    Mat4 result;
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

Mat4 Mat4::Perspective(float fovY, float aspect, float nearZ, float farZ) {
    Mat4 result;
    float tanHalfFov = tanf(fovY * 0.5f);
    result.m[0][0] = 1.0f / (aspect * tanHalfFov);
    result.m[1][1] = 1.0f / tanHalfFov;
    result.m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    result.m[2][3] = -1.0f;
    result.m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return result;
}

Mat4 Mat4::Ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    Mat4 result;
    result.m[0][0] = 2.0f / (right - left);
    result.m[1][1] = 2.0f / (top - bottom);
    result.m[2][2] = -2.0f / (farZ - nearZ);
    result.m[3][0] = -(right + left) / (right - left);
    result.m[3][1] = -(top + bottom) / (top - bottom);
    result.m[3][2] = -(farZ + nearZ) / (farZ - nearZ);
    result.m[3][3] = 1.0f;
    return result;
}

Mat4 Mat4::LookAt(float eyeX, float eyeY, float eyeZ,
                  float centerX, float centerY, float centerZ,
                  float upX, float upY, float upZ) {
    float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
    float fLen = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fLen; fy /= fLen; fz /= fLen;

    float sx = fy*upZ - fz*upY, sy = fz*upX - fx*upZ, sz = fx*upY - fy*upX;
    float sLen = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sLen; sy /= sLen; sz /= sLen;

    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;

    Mat4 result;
    result.m[0][0] = sx;  result.m[1][0] = sx;  result.m[2][0] = sx;  // NOTE: transposed
    result.m[0][0] = sx;  result.m[0][1] = ux;  result.m[0][2] = -fx;
    result.m[1][0] = sy;  result.m[1][1] = uy;  result.m[1][2] = -fy;
    result.m[2][0] = sz;  result.m[2][1] = uz;  result.m[2][2] = -fz;
    result.m[3][0] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    result.m[3][1] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    result.m[3][2] =  (fx*eyeX + fy*eyeY + fz*eyeZ);
    result.m[3][3] = 1.0f;
    return result;
}

Mat4 Mat4::Translation(float x, float y, float z) {
    Mat4 result = Identity();
    result.m[3][0] = x;
    result.m[3][1] = y;
    result.m[3][2] = z;
    return result;
}

Mat4 Mat4::Scaling(float x, float y, float z) {
    Mat4 result;
    result.m[0][0] = x;
    result.m[1][1] = y;
    result.m[2][2] = z;
    result.m[3][3] = 1.0f;
    return result;
}

Mat4 Mat4::RotationX(float radians) {
    Mat4 result = Identity();
    float c = cosf(radians), s = sinf(radians);
    result.m[1][1] = c;  result.m[1][2] = s;
    result.m[2][1] = -s; result.m[2][2] = c;
    return result;
}

Mat4 Mat4::RotationY(float radians) {
    Mat4 result = Identity();
    float c = cosf(radians), s = sinf(radians);
    result.m[0][0] = c;  result.m[0][2] = -s;
    result.m[2][0] = s;  result.m[2][2] = c;
    return result;
}

Mat4 Mat4::RotationZ(float radians) {
    Mat4 result = Identity();
    float c = cosf(radians), s = sinf(radians);
    result.m[0][0] = c;  result.m[0][1] = s;
    result.m[1][0] = -s; result.m[1][1] = c;
    return result;
}

Mat4 Mat4::operator*(const Mat4& rhs) const {
    Mat4 result;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = 0;
            for (int k = 0; k < 4; k++)
                result.m[i][j] += m[i][k] * rhs.m[k][j];
        }
    return result;
}

void Mat4::Transpose() {
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) {
            float tmp = m[i][j];
            m[i][j] = m[j][i];
            m[j][i] = tmp;
        }
}

void Mat4::Invert() {
    // Simple 4x4 matrix inverse using cofactor expansion
    float inv[16], det;
    const float* src = &m[0][0];

    inv[0]  =  src[5]*src[10]*src[15] - src[5]*src[11]*src[14] - src[9]*src[6]*src[15] + src[9]*src[7]*src[14] + src[13]*src[6]*src[11] - src[13]*src[7]*src[10];
    inv[4]  = -src[4]*src[10]*src[15] + src[4]*src[11]*src[14] + src[8]*src[6]*src[15] - src[8]*src[7]*src[14] - src[12]*src[6]*src[11] + src[12]*src[7]*src[10];
    inv[8]  =  src[4]*src[9]*src[15]  - src[4]*src[11]*src[13] - src[8]*src[5]*src[15] + src[8]*src[7]*src[13] + src[12]*src[5]*src[11] - src[12]*src[7]*src[9];
    inv[12] = -src[4]*src[9]*src[14]  + src[4]*src[10]*src[13] + src[8]*src[5]*src[14] - src[8]*src[6]*src[13] - src[12]*src[5]*src[10] + src[12]*src[6]*src[9];
    inv[1]  = -src[1]*src[10]*src[15] + src[1]*src[11]*src[14] + src[9]*src[2]*src[15] - src[9]*src[3]*src[14] - src[13]*src[2]*src[11] + src[13]*src[3]*src[10];
    inv[5]  =  src[0]*src[10]*src[15] - src[0]*src[11]*src[14] - src[8]*src[2]*src[15] + src[8]*src[3]*src[14] + src[12]*src[2]*src[11] - src[12]*src[3]*src[10];
    inv[9]  = -src[0]*src[9]*src[15]  + src[0]*src[11]*src[13] + src[8]*src[1]*src[15] - src[8]*src[3]*src[13] - src[12]*src[1]*src[11] + src[12]*src[3]*src[9];
    inv[13] =  src[0]*src[9]*src[14]  - src[0]*src[10]*src[13] - src[8]*src[1]*src[14] + src[8]*src[2]*src[13] + src[12]*src[1]*src[10] - src[12]*src[2]*src[9];
    inv[2]  =  src[1]*src[6]*src[15]  - src[1]*src[7]*src[14] - src[5]*src[2]*src[15] + src[5]*src[3]*src[14] + src[13]*src[2]*src[7]  - src[13]*src[3]*src[6];
    inv[6]  = -src[0]*src[6]*src[15]  + src[0]*src[7]*src[14] + src[4]*src[2]*src[15] - src[4]*src[3]*src[14] - src[12]*src[2]*src[7]  + src[12]*src[3]*src[6];
    inv[10] =  src[0]*src[5]*src[15]  - src[0]*src[7]*src[13] - src[4]*src[1]*src[15] + src[4]*src[3]*src[13] + src[12]*src[1]*src[7]  - src[12]*src[3]*src[5];
    inv[14] = -src[0]*src[5]*src[14]  + src[0]*src[6]*src[13] + src[4]*src[1]*src[14] - src[4]*src[2]*src[13] - src[12]*src[1]*src[6]  + src[12]*src[2]*src[5];
    inv[3]  = -src[1]*src[6]*src[11]  + src[1]*src[7]*src[10] + src[5]*src[2]*src[11] - src[5]*src[3]*src[10] - src[9]*src[2]*src[7]   + src[9]*src[3]*src[6];
    inv[7]  =  src[0]*src[6]*src[11]  - src[0]*src[7]*src[10] - src[4]*src[2]*src[11] + src[4]*src[3]*src[10] + src[8]*src[2]*src[7]   - src[8]*src[3]*src[6];
    inv[11] = -src[0]*src[5]*src[11]  + src[0]*src[7]*src[9]  + src[4]*src[1]*src[11] - src[4]*src[3]*src[9]  - src[8]*src[1]*src[7]   + src[8]*src[3]*src[5];
    inv[15] =  src[0]*src[5]*src[10]  - src[0]*src[6]*src[9]  - src[4]*src[1]*src[10] + src[4]*src[2]*src[9]  + src[8]*src[1]*src[6]   - src[8]*src[2]*src[5];

    det = src[0]*inv[0] + src[1]*inv[4] + src[2]*inv[8] + src[3]*inv[12];
    if (fabsf(det) < 1e-10f) return;

    det = 1.0f / det;
    float* dst = &m[0][0];
    for (int i = 0; i < 16; i++)
        dst[i] = inv[i] * det;
}

// ===========================================================================
// ShaderProgram implementation
// ===========================================================================
ShaderProgram::ShaderProgram() : m_Program(0) {}

ShaderProgram::~ShaderProgram() {
    if (m_Program) {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }
}

GLuint ShaderProgram::CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* buf = new char[infoLen];
            glGetShaderInfoLog(shader, infoLen, nullptr, buf);
            LOGE("Shader compile error: %s", buf);
            delete[] buf;
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::Compile(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_Program = glCreateProgram();
    glAttachShader(m_Program, vs);
    glAttachShader(m_Program, fs);
    glLinkProgram(m_Program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(m_Program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* buf = new char[infoLen];
            glGetProgramInfoLog(m_Program, infoLen, nullptr, buf);
            LOGE("Shader link error: %s", buf);
            delete[] buf;
        }
        glDeleteProgram(m_Program);
        m_Program = 0;
        return false;
    }

    return true;
}

void ShaderProgram::Use() const {
    glUseProgram(m_Program);
}

GLint ShaderProgram::GetUniformLocation(const char* name) const {
    return glGetUniformLocation(m_Program, name);
}

GLint ShaderProgram::GetAttribLocation(const char* name) const {
    return glGetAttribLocation(m_Program, name);
}

void ShaderProgram::SetUniform1i(const char* name, int value) {
    glUniform1i(GetUniformLocation(name), value);
}
void ShaderProgram::SetUniform1f(const char* name, float value) {
    glUniform1f(GetUniformLocation(name), value);
}
void ShaderProgram::SetUniform2f(const char* name, float x, float y) {
    glUniform2f(GetUniformLocation(name), x, y);
}
void ShaderProgram::SetUniform3f(const char* name, float x, float y, float z) {
    glUniform3f(GetUniformLocation(name), x, y, z);
}
void ShaderProgram::SetUniform4f(const char* name, float x, float y, float z, float w) {
    glUniform4f(GetUniformLocation(name), x, y, z, w);
}
void ShaderProgram::SetUniformMat4(const char* name, const Mat4& matrix) {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, matrix.Ptr());
}
void ShaderProgram::SetUniformMat4(const char* name, const float* matrix) {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, matrix);
}

// ===========================================================================
// TextureGLES implementation
// ===========================================================================
TextureGLES::TextureGLES() : m_Texture(0), m_Width(0), m_Height(0), m_Format(GL_RGBA8) {}

TextureGLES::~TextureGLES() {
    if (m_Texture) {
        glDeleteTextures(1, &m_Texture);
        m_Texture = 0;
    }
}

bool TextureGLES::CreateFromData(const uint8_t* data, int width, int height, int channels, bool generateMips) {
    GLenum format, internalFormat;
    switch (channels) {
        case 1: format = GL_RED;  internalFormat = GL_R8;    break;
        case 3: format = GL_RGB;  internalFormat = GL_RGB8;  break;
        case 4: format = GL_RGBA; internalFormat = GL_RGBA8; break;
        default: return false;
    }

    glGenTextures(1, &m_Texture);
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    if (generateMips)
        glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    m_Width = width;
    m_Height = height;
    m_Format = internalFormat;
    return true;
}

bool TextureGLES::CreateRenderTarget(int width, int height, GLenum format) {
    glGenTextures(1, &m_Texture);
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_Width = width;
    m_Height = height;
    m_Format = format;
    return true;
}

void TextureGLES::Bind(int textureUnit) const {
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, m_Texture);
}

void TextureGLES::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureGLES::SetFilterMode(GLenum minFilter, GLenum magFilter) {
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
}

void TextureGLES::SetWrapMode(GLenum wrapS, GLenum wrapT) {
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
}

// ===========================================================================
// VertexBufferGLES implementation
// ===========================================================================
VertexBufferGLES::VertexBufferGLES() : m_VBO(0), m_Size(0) {}
VertexBufferGLES::~VertexBufferGLES() {
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
}

bool VertexBufferGLES::Create(const void* data, uint32_t sizeBytes, GLenum usage) {
    glGenBuffers(1, &m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeBytes, data, usage);
    m_Size = sizeBytes;
    return true;
}

void VertexBufferGLES::Update(const void* data, uint32_t offsetBytes, uint32_t sizeBytes) {
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, offsetBytes, sizeBytes, data);
}

void VertexBufferGLES::Bind() const { glBindBuffer(GL_ARRAY_BUFFER, m_VBO); }
void VertexBufferGLES::Unbind() const { glBindBuffer(GL_ARRAY_BUFFER, 0); }

// ===========================================================================
// IndexBufferGLES implementation
// ===========================================================================
IndexBufferGLES::IndexBufferGLES() : m_IBO(0), m_Count(0), m_IndexType(GL_UNSIGNED_SHORT) {}
IndexBufferGLES::~IndexBufferGLES() {
    if (m_IBO) glDeleteBuffers(1, &m_IBO);
}

bool IndexBufferGLES::Create(const void* data, uint32_t count, GLenum indexType, GLenum usage) {
    uint32_t indexSize = (indexType == GL_UNSIGNED_INT) ? 4 : 2;
    glGenBuffers(1, &m_IBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * indexSize, data, usage);
    m_Count = count;
    m_IndexType = indexType;
    return true;
}

void IndexBufferGLES::Bind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO); }
void IndexBufferGLES::Unbind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }

// ===========================================================================
// FrameBufferGLES implementation
// ===========================================================================
FrameBufferGLES::FrameBufferGLES() : m_FBO(0), m_ColorTexture(0), m_DepthStencilRBO(0), m_Width(0), m_Height(0) {}
FrameBufferGLES::~FrameBufferGLES() {
    if (m_DepthStencilRBO) glDeleteRenderbuffers(1, &m_DepthStencilRBO);
    if (m_ColorTexture) glDeleteTextures(1, &m_ColorTexture);
    if (m_FBO) glDeleteFramebuffers(1, &m_FBO);
}

bool FrameBufferGLES::Create(int width, int height, bool hasDepth, bool hasStencil) {
    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    // Color attachment
    glGenTextures(1, &m_ColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

    // Depth/stencil attachment
    if (hasDepth || hasStencil) {
        glGenRenderbuffers(1, &m_DepthStencilRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, m_DepthStencilRBO);
        GLenum depthFormat = hasStencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
        glRenderbufferStorage(GL_RENDERBUFFER, depthFormat, width, height);
        GLenum attachment = hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, m_DepthStencilRBO);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_Width = width;
    m_Height = height;

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("Framebuffer incomplete: 0x%x", status);
        return false;
    }
    return true;
}

void FrameBufferGLES::Bind() const { glBindFramebuffer(GL_FRAMEBUFFER, m_FBO); }
void FrameBufferGLES::Unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
void FrameBufferGLES::BindDefault() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

// ===========================================================================
// VertexArrayGLES implementation
// ===========================================================================
VertexArrayGLES::VertexArrayGLES() : m_VAO(0) {}
VertexArrayGLES::~VertexArrayGLES() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
}

void VertexArrayGLES::Create() { glGenVertexArrays(1, &m_VAO); }
void VertexArrayGLES::Bind() const { glBindVertexArray(m_VAO); }
void VertexArrayGLES::Unbind() const { glBindVertexArray(0); }

void VertexArrayGLES::SetupPosColorUV() {
    // Stride: 3f(pos) + 4ub(color) + 2f(uv) = 24 bytes
    const int stride = 24;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)16);
}

void VertexArrayGLES::SetupPosNormalUV() {
    // Stride: 3f(pos) + 3f(normal) + 2f(uv) = 32 bytes
    const int stride = 32;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)24);
}

void VertexArrayGLES::SetupPosColor() {
    // Stride: 3f(pos) + 4ub(color) = 16 bytes
    const int stride = 16;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)12);
}

void VertexArrayGLES::SetupPos2DColorUV() {
    // Stride: 2f(pos) + 4ub(color) + 2f(uv) = 20 bytes
    const int stride = 20;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)12);
}

// ===========================================================================
// GfxInternal_GLES implementation
// ===========================================================================
GfxInternal_GLES::GfxInternal_GLES()
    : m_Width(0), m_Height(0)
    , m_DynamicVAO(0), m_DynamicVBO(0)
{
    m_ProjectionMatrix = Mat4::Identity();
    m_ViewMatrix = Mat4::Identity();
    m_WorldMatrix = Mat4::Identity();
}

GfxInternal_GLES::~GfxInternal_GLES() {
    Shutdown();
}

bool GfxInternal_GLES::Initialize(int width, int height) {
    m_Width = width;
    m_Height = height;
    s_Instance = this;

    LOGI("GfxInternal_GLES::Initialize %dx%d", width, height);
    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));

    // Set default GL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Create dynamic draw buffers
    glGenVertexArrays(1, &m_DynamicVAO);
    glGenBuffers(1, &m_DynamicVBO);
    glBindVertexArray(m_DynamicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_DynamicVBO);
    glBufferData(GL_ARRAY_BUFFER, 64 * 1024, nullptr, GL_DYNAMIC_DRAW); // 64KB dynamic buffer
    glBindVertexArray(0);

    // Compile built-in shaders
    if (!CompileBuiltinShaders()) {
        LOGE("Failed to compile built-in shaders");
        return false;
    }

    return true;
}

void GfxInternal_GLES::Shutdown() {
    if (m_DynamicVBO) { glDeleteBuffers(1, &m_DynamicVBO); m_DynamicVBO = 0; }
    if (m_DynamicVAO) { glDeleteVertexArrays(1, &m_DynamicVAO); m_DynamicVAO = 0; }
    s_Instance = nullptr;
}

bool GfxInternal_GLES::CompileBuiltinShaders() {
    if (!m_Shader2D.Compile(s_Vertex2D, s_Fragment2D)) {
        LOGE("Failed to compile 2D shader");
        return false;
    }
    if (!m_Shader3D.Compile(s_Vertex3D, s_Fragment3D)) {
        LOGE("Failed to compile 3D shader");
        return false;
    }
    if (!m_Shader3DTextured.Compile(s_Vertex3DTex, s_Fragment3DTex)) {
        LOGE("Failed to compile 3D textured shader");
        return false;
    }
    LOGI("Built-in shaders compiled successfully");
    return true;
}

void GfxInternal_GLES::BeginFrame() {
    glViewport(0, 0, m_Width, m_Height);
}

void GfxInternal_GLES::EndFrame() {
    // Swap is handled by platform layer
}

void GfxInternal_GLES::Clear(float r, float g, float b, float a, float depth) {
    glClearColor(r, g, b, a);
    glClearDepthf(depth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GfxInternal_GLES::SetViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void GfxInternal_GLES::SetBlendEnabled(bool enabled) {
    if (enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void GfxInternal_GLES::SetDepthTestEnabled(bool enabled) {
    if (enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void GfxInternal_GLES::SetDepthWriteEnabled(bool enabled) {
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void GfxInternal_GLES::SetCullFaceEnabled(bool enabled) {
    if (enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

void GfxInternal_GLES::SetCullFace(GLenum face) {
    glCullFace(face);
}

void GfxInternal_GLES::SetBlendFunc(GLenum src, GLenum dst) {
    glBlendFunc(src, dst);
}

void GfxInternal_GLES::SetProjectionMatrix(const Mat4& mat) { m_ProjectionMatrix = mat; }
void GfxInternal_GLES::SetViewMatrix(const Mat4& mat) { m_ViewMatrix = mat; }
void GfxInternal_GLES::SetWorldMatrix(const Mat4& mat) { m_WorldMatrix = mat; }

void GfxInternal_GLES::SetTexture(int unit, TextureGLES* texture) {
    if (texture)
        texture->Bind(unit);
    else {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void GfxInternal_GLES::DrawTexturedQuad2D(float x, float y, float w, float h,
                                            float u0, float v0, float u1, float v1,
                                            D3DCOLOR color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color)       & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    // Pos2D(2f) + Color(4ub) + UV(2f) = 20 bytes per vertex, 6 vertices (2 triangles)
    struct Vert {
        float px, py;
        uint8_t cr, cg, cb, ca;
        float u, v;
    };

    Vert verts[6] = {
        { x,     y,     r, g, b, a, u0, v0 },
        { x + w, y,     r, g, b, a, u1, v0 },
        { x,     y + h, r, g, b, a, u0, v1 },
        { x + w, y,     r, g, b, a, u1, v0 },
        { x + w, y + h, r, g, b, a, u1, v1 },
        { x,     y + h, r, g, b, a, u0, v1 },
    };

    m_Shader2D.Use();

    // Orthographic projection for 2D
    Mat4 ortho = Mat4::Ortho(0.0f, (float)m_Width, (float)m_Height, 0.0f, -1.0f, 1.0f);
    m_Shader2D.SetUniformMat4("u_Projection", ortho);
    m_Shader2D.SetUniform1i("u_UseTexture", 1);
    m_Shader2D.SetUniform1i("u_Texture", 0);

    glBindVertexArray(m_DynamicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_DynamicVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    // Setup vertex attributes: pos(2f), color(4ub), uv(2f)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 20, (void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
