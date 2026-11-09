#pragma once

#include <Geode/Geode.hpp>

#include "gl.hpp"

struct RenderPass {
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    const char* m_vertexShader = nullptr;
    const char* m_fragmentShader = nullptr;

    GLuint m_program = 0;
    GLuint m_fbo = 0;
    GLuint m_tex = 0;

    void initialize();
    void resize();
    void destroy();
};
