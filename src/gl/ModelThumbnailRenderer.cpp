#include "gl/ModelThumbnailRenderer.h"
#include "model/ModelGeometry.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QMatrix4x4>
#include <QVector3D>
#include <QSurfaceFormat>
#include <cmath>
#include <vector>

namespace {
const char* kVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;
uniform mat4 uMVP; uniform mat4 uModel;
out vec3 vN; out vec2 vUV;
void main(){ vN = mat3(uModel) * aNrm; vUV = aUV; gl_Position = uMVP * vec4(aPos, 1.0); }
)";
const char* kFrag = R"(#version 330 core
in vec3 vN; in vec2 vUV; out vec4 FragColor;
uniform bool uHasTex; uniform sampler2D uTex;
void main(){
    vec3 n = normalize(vN);
    // Two soft lights + ambient so both near and far faces read.
    float d = max(dot(n, normalize(vec3(0.4, 0.8, 0.6))), 0.0) * 0.7
            + max(dot(n, normalize(vec3(-0.5, 0.2, -0.7))), 0.0) * 0.25
            + 0.28;
    vec3 base = uHasTex ? texture(uTex, vUV).rgb : vec3(0.72);
    FragColor = vec4(base * clamp(d, 0.0, 1.0), 1.0);
}
)";
}  // namespace

ModelThumbnailRenderer::ModelThumbnailRenderer(int size) : m_size(qMax(32, size))
{
    QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
    m_surface.setFormat(f); m_surface.create();
    m_ctx.setFormat(f);
}

ModelThumbnailRenderer::~ModelThumbnailRenderer()
{
    if (m_init && m_ctx.makeCurrent(&m_surface) && m_gl) {
        if (m_prog) m_gl->glDeleteProgram(m_prog);
        if (m_vbo) m_gl->glDeleteBuffers(1, &m_vbo);
        if (m_ibo) m_gl->glDeleteBuffers(1, &m_ibo);
        if (m_vao) m_gl->glDeleteVertexArrays(1, &m_vao);
        if (m_col) m_gl->glDeleteTextures(1, &m_col);
        if (m_dep) m_gl->glDeleteRenderbuffers(1, &m_dep);
        if (m_fbo) m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_ctx.doneCurrent();
    }
}

bool ModelThumbnailRenderer::ensureGl()
{
    if (m_init) return m_ok;
    m_init = true;
    if (!m_surface.isValid() || !m_ctx.create() || !m_ctx.makeCurrent(&m_surface)) return (m_ok = false);
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(&m_ctx);
    if (!m_gl) return (m_ok = false);
    m_gl->initializeOpenGLFunctions();

    auto compile = [&](unsigned type, const char* src) -> unsigned {
        unsigned s = m_gl->glCreateShader(type);
        m_gl->glShaderSource(s, 1, &src, nullptr); m_gl->glCompileShader(s);
        int okc = 0; m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &okc);
        return okc ? s : 0;
    };
    unsigned vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return (m_ok = false);
    m_prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(m_prog, vs); m_gl->glAttachShader(m_prog, fs); m_gl->glLinkProgram(m_prog);
    m_gl->glDeleteShader(vs); m_gl->glDeleteShader(fs);
    int okl = 0; m_gl->glGetProgramiv(m_prog, GL_LINK_STATUS, &okl);
    if (!okl) return (m_ok = false);
    m_uMVP = m_gl->glGetUniformLocation(m_prog, "uMVP");
    m_uModel = m_gl->glGetUniformLocation(m_prog, "uModel");
    m_uHasTex = m_gl->glGetUniformLocation(m_prog, "uHasTex");
    m_uTex = m_gl->glGetUniformLocation(m_prog, "uTex");

    m_gl->glGenFramebuffers(1, &m_fbo); m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glGenTextures(1, &m_col); m_gl->glBindTexture(GL_TEXTURE_2D, m_col);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_size, m_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_col, 0);
    m_gl->glGenRenderbuffers(1, &m_dep); m_gl->glBindRenderbuffer(GL_RENDERBUFFER, m_dep);
    m_gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_size, m_size);
    m_gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_dep);
    const bool fboOk = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glGenBuffers(1, &m_ibo);
    return (m_ok = fboOk);
}

QImage ModelThumbnailRenderer::render(const ModelGeometry& geo, const QVector<QImage>* baseColors)
{
    if (geo.vertices.isEmpty() || geo.indices.isEmpty()) return QImage();
    if (!ensureGl()) return QImage();
    if (!m_ctx.makeCurrent(&m_surface)) return QImage();

    const bool textured = baseColors && !baseColors->isEmpty();

    // Interleaved pos3 + nrm3 + uv2, converted from PoE2 Z-up to Y-up (x, z, -y).
    std::vector<float> vb; vb.reserve(size_t(geo.vertices.size()) * 8);
    for (const MeshVertex& v : geo.vertices) {
        vb.push_back(v.px); vb.push_back(v.pz); vb.push_back(-v.py);
        vb.push_back(v.nx); vb.push_back(v.nz); vb.push_back(-v.ny);
        vb.push_back(v.u);  vb.push_back(v.v);
    }
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vb.size() * 4), vb.data(), GL_STREAM_DRAW);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(geo.indices.size() * 4), geo.indices.constData(), GL_STREAM_DRAW);
    m_gl->glEnableVertexAttribArray(0); m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    m_gl->glEnableVertexAttribArray(1); m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    m_gl->glEnableVertexAttribArray(2); m_gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);

    // Frame the bounding box (Y-up). computeBounds() may not have run; derive from the converted verts.
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (size_t i = 0; i + 2 < vb.size(); i += 8) {
        lo.setX(qMin(lo.x(), vb[i])); lo.setY(qMin(lo.y(), vb[i+1])); lo.setZ(qMin(lo.z(), vb[i+2]));
        hi.setX(qMax(hi.x(), vb[i])); hi.setY(qMax(hi.y(), vb[i+1])); hi.setZ(qMax(hi.z(), vb[i+2]));
    }
    const QVector3D c = (lo + hi) * 0.5f;
    float rad = (hi - lo).length() * 0.5f; if (rad < 1e-3f) rad = 1.0f;
    const float yaw = 0.6f;   // three-quarter view
    const QVector3D eye = c + QVector3D(std::sin(yaw), 0.35f, std::cos(yaw)) * rad * 2.6f;
    QMatrix4x4 P; P.perspective(35.0f, 1.0f, rad * 0.05f, rad * 20.0f);
    QMatrix4x4 V; V.lookAt(eye, c, QVector3D(0, 1, 0));
    QMatrix4x4 M;
    const QMatrix4x4 mvp = P * V * M;

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glViewport(0, 0, m_size, m_size);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_BLEND);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);   // transparent — the grid supplies the backdrop
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_gl->glUseProgram(m_prog);
    m_gl->glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp.constData());
    m_gl->glUniformMatrix4fv(m_uModel, 1, GL_FALSE, M.constData());
    m_gl->glUniform1i(m_uTex, 0);

    if (!textured) {
        m_gl->glUniform1i(m_uHasTex, 0);
        m_gl->glDrawElements(GL_TRIANGLES, geo.indices.size(), GL_UNSIGNED_INT, nullptr);
    } else {
        // Upload each material's base colour once, then draw parts grouped by material.
        QVector<unsigned> tex(baseColors->size(), 0);
        for (int i = 0; i < baseColors->size(); ++i) {
            const QImage& src = (*baseColors)[i];
            if (src.isNull()) continue;
            const QImage im = src.convertToFormat(QImage::Format_RGBA8888);
            unsigned t = 0; m_gl->glGenTextures(1, &t); m_gl->glBindTexture(GL_TEXTURE_2D, t);
            m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.width(), im.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, im.constBits());
            m_gl->glGenerateMipmap(GL_TEXTURE_2D);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            tex[i] = t;
        }
        m_gl->glActiveTexture(GL_TEXTURE0);
        for (const MeshPart& p : geo.parts) {
            const unsigned t = (p.materialIndex >= 0 && p.materialIndex < tex.size()) ? tex[p.materialIndex] : 0;
            m_gl->glUniform1i(m_uHasTex, t ? 1 : 0);
            if (t) m_gl->glBindTexture(GL_TEXTURE_2D, t);
            m_gl->glDrawElements(GL_TRIANGLES, p.indexCount, GL_UNSIGNED_INT, (void*)(size_t(p.indexStart) * 4));
        }
        for (unsigned t : tex) if (t) m_gl->glDeleteTextures(1, &t);
    }

    QImage img(m_size, m_size, QImage::Format_RGBA8888);
    m_gl->glReadPixels(0, 0, m_size, m_size, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // The Y-up conversion (x, z, -y) already lands the model upright in read order, so NO extra vertical
    // flip — mirroring here turned every thumbnail upside down.
    return img;
}
