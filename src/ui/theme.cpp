#include "theme.hpp"

#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "hook.hpp"
#include "manager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"

#ifndef GEODE_IS_MOBILE
const char* THEME_BASE_VERT = R"(#version 130
#extension GL_ARB_explicit_attrib_location : require

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_texCoord;

out vec2 v_texCoord;

void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";
#endif

void Theme::initialize() {
#ifdef GEODE_IS_MOBILE
    return;
#endif

    int width, height, channels;
    const auto iconPath =
        (geode::Mod::get()->getResourcesDir() / m_iconPath).string();
    uint8_t* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 0);
    if (!data) {
        return;
    }

    GLint oldActiveTexture;
    GLint oldTexture;
    GLint oldUnpackAlignment;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldUnpackAlignment);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (channels == 4) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, data);
    }
    stbi_image_free(data);

    glPixelStorei(GL_UNPACK_ALIGNMENT, oldUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    glActiveTexture(oldActiveTexture);

#if defined(GEODE_IS_MOBILE) || defined(GEODE_IS_MACOS)
    return;
#else
    m_postprocessPass =
        RenderPass{.m_width = (unsigned int)ImGuiHookCtx::get().m_width,
                   .m_height = (unsigned int)ImGuiHookCtx::get().m_height,
                   .m_vertexShader = THEME_BASE_VERT,
                   .m_fragmentShader = m_postprocessShader.c_str()};
    m_postprocessPass.initialize();
#endif
}

void Theme::resize(uint32_t width, uint32_t height) {
#if defined(GEODE_IS_MOBILE) || defined(GEODE_IS_MACOS)
    (void)width;
    (void)height;
    return;
#else
    m_postprocessPass.m_width = width;
    m_postprocessPass.m_height = height;
    m_postprocessPass.resize();
#endif
}

void Theme::apply() { GrapeEngine::get()->ui().m_texture = m_texture; }
