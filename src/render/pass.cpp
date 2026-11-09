#include "pass.hpp"

#include <array>
#include <utility>

namespace {

class KeepBindings {
   public:
    KeepBindings() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_framebuffer);
    }
    ~KeepBindings() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_framebuffer));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture));
    }

   private:
    GLint m_texture = 0;
    GLint m_framebuffer = 0;
};

template <class Query, class ReadLog>
bool reportFailure(GLuint object, GLenum status, Query query, ReadLog readLog, const char* stage) {
    GLint ok = GL_FALSE;
    query(object, status, &ok);
    if (ok) return false;
    std::array<char, 512> log{};
    readLog(object, static_cast<GLsizei>(log.size() - 1), nullptr, log.data());
    geode::log::error("render pass {} failed: {}", stage, log.data());
    return true;
}

GLuint linkProgram(const char* vertexSource, const char* fragmentSource) {
    const std::pair<GLenum, const char*> stages[] = {
        {GL_VERTEX_SHADER, vertexSource},
        {GL_FRAGMENT_SHADER, fragmentSource},
    };
    const GLuint program = glCreateProgram();
    bool broken = false;
    for (const auto& [kind, source] : stages) {
        const GLuint shader = glCreateShader(kind);
        const GLchar* text = source;
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        broken |= reportFailure(shader, GL_COMPILE_STATUS, glGetShaderiv, glGetShaderInfoLog, "compile");
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    if (!broken) {
        glLinkProgram(program);
        if (!reportFailure(program, GL_LINK_STATUS, glGetProgramiv, glGetProgramInfoLog, "link")) return program;
    }
    glDeleteProgram(program);
    return 0;
}

void allocate(GLuint texture, uint32_t width, uint32_t height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
}

}

void RenderPass::initialize() {
    KeepBindings keep;

    glGenTextures(1, &m_tex);
    allocate(m_tex, m_width, m_height);
    constexpr std::pair<GLenum, GLint> sampling[] = {
        {GL_TEXTURE_MIN_FILTER, GL_LINEAR},
        {GL_TEXTURE_MAG_FILTER, GL_LINEAR},
        {GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE},
        {GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE},
    };
    for (const auto& [name, value] : sampling) glTexParameteri(GL_TEXTURE_2D, name, value);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);

    if (m_vertexShader && m_fragmentShader) m_program = linkProgram(m_vertexShader, m_fragmentShader);
}

void RenderPass::resize() {
    KeepBindings keep;
    allocate(m_tex, m_width, m_height);
}

void RenderPass::destroy() {
    glDeleteProgram(m_program);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_tex);
    m_program = m_fbo = m_tex = 0;
}
