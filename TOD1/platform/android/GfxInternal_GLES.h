/*
 * GfxInternal_GLES.h
 *
 * OpenGL ES 3.0 graphics backend replacing GfxInternal_Dx9.
 * Provides equivalent rendering functionality using GLES instead of Direct3D 9.
 */
#pragma once

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <cstdint>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// D3D -> GLES type mappings
// ---------------------------------------------------------------------------
typedef uint32_t D3DCOLOR;

#define D3DCOLOR_ARGB(a, r, g, b) \
    ((D3DCOLOR)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b)   D3DCOLOR_ARGB(0xff, r, g, b)

// ---------------------------------------------------------------------------
// Simple 4x4 matrix (replaces DirectX::XMMATRIX for Android)
// ---------------------------------------------------------------------------
struct Mat4 {
    float m[4][4];

    Mat4();
    static Mat4 Identity();
    static Mat4 Perspective(float fovY, float aspect, float nearZ, float farZ);
    static Mat4 Ortho(float left, float right, float bottom, float top, float nearZ, float farZ);
    static Mat4 LookAt(float eyeX, float eyeY, float eyeZ,
                        float centerX, float centerY, float centerZ,
                        float upX, float upY, float upZ);
    static Mat4 Translation(float x, float y, float z);
    static Mat4 Scaling(float x, float y, float z);
    static Mat4 RotationX(float radians);
    static Mat4 RotationY(float radians);
    static Mat4 RotationZ(float radians);

    Mat4 operator*(const Mat4& rhs) const;
    void Transpose();
    void Invert();
    const float* Ptr() const { return &m[0][0]; }
};

// ---------------------------------------------------------------------------
// Shader program wrapper
// ---------------------------------------------------------------------------
class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    bool    Compile(const char* vertexSrc, const char* fragmentSrc);
    void    Use() const;
    GLint   GetUniformLocation(const char* name) const;
    GLint   GetAttribLocation(const char* name) const;
    GLuint  GetProgram() const { return m_Program; }

    // Uniform setters
    void    SetUniform1i(const char* name, int value);
    void    SetUniform1f(const char* name, float value);
    void    SetUniform2f(const char* name, float x, float y);
    void    SetUniform3f(const char* name, float x, float y, float z);
    void    SetUniform4f(const char* name, float x, float y, float z, float w);
    void    SetUniformMat4(const char* name, const Mat4& matrix);
    void    SetUniformMat4(const char* name, const float* matrix);

private:
    GLuint  CompileShader(GLenum type, const char* source);
    GLuint  m_Program;
};

// ---------------------------------------------------------------------------
// Texture wrapper (replaces IDirect3DTexture9)
// ---------------------------------------------------------------------------
class TextureGLES {
public:
    TextureGLES();
    ~TextureGLES();

    bool    CreateFromData(const uint8_t* data, int width, int height, int channels, bool generateMips = true);
    bool    CreateRenderTarget(int width, int height, GLenum format = GL_RGBA8);
    void    Bind(int textureUnit = 0) const;
    void    Unbind() const;

    GLuint  GetHandle() const { return m_Texture; }
    int     GetWidth() const { return m_Width; }
    int     GetHeight() const { return m_Height; }

    void    SetFilterMode(GLenum minFilter, GLenum magFilter);
    void    SetWrapMode(GLenum wrapS, GLenum wrapT);

private:
    GLuint  m_Texture;
    int     m_Width;
    int     m_Height;
    GLenum  m_Format;
};

// ---------------------------------------------------------------------------
// Vertex/Index buffer wrappers (replaces IDirect3DVertexBuffer9/IndexBuffer9)
// ---------------------------------------------------------------------------
class VertexBufferGLES {
public:
    VertexBufferGLES();
    ~VertexBufferGLES();

    bool    Create(const void* data, uint32_t sizeBytes, GLenum usage = GL_STATIC_DRAW);
    void    Update(const void* data, uint32_t offsetBytes, uint32_t sizeBytes);
    void    Bind() const;
    void    Unbind() const;
    GLuint  GetHandle() const { return m_VBO; }

private:
    GLuint  m_VBO;
    uint32_t m_Size;
};

class IndexBufferGLES {
public:
    IndexBufferGLES();
    ~IndexBufferGLES();

    bool    Create(const void* data, uint32_t count, GLenum indexType = GL_UNSIGNED_SHORT, GLenum usage = GL_STATIC_DRAW);
    void    Bind() const;
    void    Unbind() const;
    GLuint  GetHandle() const { return m_IBO; }
    uint32_t GetCount() const { return m_Count; }
    GLenum  GetIndexType() const { return m_IndexType; }

private:
    GLuint  m_IBO;
    uint32_t m_Count;
    GLenum  m_IndexType;
};

// ---------------------------------------------------------------------------
// Framebuffer object (replaces D3D render targets)
// ---------------------------------------------------------------------------
class FrameBufferGLES {
public:
    FrameBufferGLES();
    ~FrameBufferGLES();

    bool    Create(int width, int height, bool hasDepth = true, bool hasStencil = false);
    void    Bind() const;
    void    Unbind() const;
    GLuint  GetColorTexture() const { return m_ColorTexture; }
    int     GetWidth() const { return m_Width; }
    int     GetHeight() const { return m_Height; }

    static void BindDefault();

private:
    GLuint  m_FBO;
    GLuint  m_ColorTexture;
    GLuint  m_DepthStencilRBO;
    int     m_Width;
    int     m_Height;
};

// ---------------------------------------------------------------------------
// Vertex Array Object (VAO) - manages vertex attribute state
// ---------------------------------------------------------------------------
class VertexArrayGLES {
public:
    VertexArrayGLES();
    ~VertexArrayGLES();

    void    Create();
    void    Bind() const;
    void    Unbind() const;

    // Standard attribute layout for engine vertices
    void    SetupPosColorUV();          // position(3f) + color(4ub) + uv(2f)
    void    SetupPosNormalUV();         // position(3f) + normal(3f) + uv(2f)
    void    SetupPosColor();            // position(3f) + color(4ub)
    void    SetupPos2DColorUV();        // position(2f) + color(4ub) + uv(2f)

private:
    GLuint  m_VAO;
};

// ---------------------------------------------------------------------------
// GfxInternal_GLES - main renderer (replaces GfxInternal_Dx9)
// ---------------------------------------------------------------------------
class GfxInternal_GLES {
public:
    GfxInternal_GLES();
    ~GfxInternal_GLES();

    bool    Initialize(int width, int height);
    void    Shutdown();

    // Frame management
    void    BeginFrame();
    void    EndFrame();
    void    Clear(float r, float g, float b, float a, float depth = 1.0f);
    void    SetViewport(int x, int y, int width, int height);

    // State management
    void    SetBlendEnabled(bool enabled);
    void    SetDepthTestEnabled(bool enabled);
    void    SetDepthWriteEnabled(bool enabled);
    void    SetCullFaceEnabled(bool enabled);
    void    SetCullFace(GLenum face);
    void    SetBlendFunc(GLenum src, GLenum dst);

    // Matrix management (compatibility with engine's matrix stack)
    void    SetProjectionMatrix(const Mat4& mat);
    void    SetViewMatrix(const Mat4& mat);
    void    SetWorldMatrix(const Mat4& mat);
    Mat4    GetProjectionMatrix() const { return m_ProjectionMatrix; }
    Mat4    GetViewMatrix() const { return m_ViewMatrix; }
    Mat4    GetWorldMatrix() const { return m_WorldMatrix; }

    // Built-in shader programs
    ShaderProgram*  GetShader2D() { return &m_Shader2D; }
    ShaderProgram*  GetShader3D() { return &m_Shader3D; }
    ShaderProgram*  GetShader3DTextured() { return &m_Shader3DTextured; }

    // Primitive drawing (replaces DX9 DrawPrimitive calls)
    void    DrawLines2D(const float* vertices, int count, D3DCOLOR color);
    void    DrawLines3D(const float* vertices, int count, D3DCOLOR color);
    void    DrawTriangles2D(const float* vertices, int count);
    void    DrawTriangles3D(const float* vertices, int count);
    void    DrawTexturedQuad2D(float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1,
                               D3DCOLOR color);

    // Texture management
    void    SetTexture(int unit, TextureGLES* texture);

    // Screen info
    int     GetWidth() const { return m_Width; }
    int     GetHeight() const { return m_Height; }
    float   GetAspectRatio() const { return (float)m_Width / (float)m_Height; }

    // Singleton
    static GfxInternal_GLES* GetInstance() { return s_Instance; }

private:
    bool    CompileBuiltinShaders();

    int     m_Width;
    int     m_Height;

    // Matrices
    Mat4    m_ProjectionMatrix;
    Mat4    m_ViewMatrix;
    Mat4    m_WorldMatrix;

    // Built-in shaders
    ShaderProgram m_Shader2D;
    ShaderProgram m_Shader3D;
    ShaderProgram m_Shader3DTextured;

    // Dynamic draw buffer
    GLuint  m_DynamicVAO;
    GLuint  m_DynamicVBO;

    static GfxInternal_GLES* s_Instance;
};
