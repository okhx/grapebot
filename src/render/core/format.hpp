#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../gl.hpp"

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace grape::render {

inline constexpr char const* PLANE_VERTEX_SHADER = R"(#version 130
in vec4 a_position;
in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texCoord = vec2(a_texCoord.x, 1.0 - a_texCoord.y);
}
)";

inline constexpr char const* FADE_FRAGMENT_SHADER = R"(#version 130
precision highp float;
uniform vec4 u_color;
void main() {
    gl_FragData[0] = u_color;
}
)";

inline constexpr char const* SHADER_Y = R"(#version 130
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_texture;
const vec3 coeffY = vec3(0.2126, 0.7152, 0.0722);
void main() {
    vec3 rgb = texture2D(u_texture, v_texCoord).rgb;
    float y = dot(rgb, coeffY) * 219.0 / 255.0 + 16.0 / 255.0;
    gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);
}
)";

inline constexpr char const* SHADER_UV = R"(#version 130
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_texture;
const vec3 coeffU = vec3(-0.11457, -0.38543, 0.5);
const vec3 coeffV = vec3(0.5, -0.45415, -0.04585);
void main() {
    vec3 rgb = texture2D(u_texture, v_texCoord).rgb;
    float u = (dot(rgb, coeffU) + 0.5) * 224.0 / 255.0 + 16.0 / 255.0;
    float v = (dot(rgb, coeffV) + 0.5) * 224.0 / 255.0 + 16.0 / 255.0;
    gl_FragData[0] = vec4(u, v, 0.0, 1.0);
}
)";

inline constexpr char const* SHADER_U = R"(#version 130
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_texture;
const vec3 coeffU = vec3(-0.11457, -0.38543, 0.5);
void main() {
    vec3 rgb = texture2D(u_texture, v_texCoord).rgb;
    float u = (dot(rgb, coeffU) + 0.5) * 224.0 / 255.0 + 16.0 / 255.0;
    gl_FragData[0] = vec4(u, 0.0, 0.0, 1.0);
}
)";

inline constexpr char const* SHADER_V = R"(#version 130
precision highp float;
in vec2 v_texCoord;
uniform sampler2D u_texture;
const vec3 coeffV = vec3(0.5, -0.45415, -0.04585);
void main() {
    vec3 rgb = texture2D(u_texture, v_texCoord).rgb;
    float v = (dot(rgb, coeffV) + 0.5) * 224.0 / 255.0 + 16.0 / 255.0;
    gl_FragData[0] = vec4(v, 0.0, 0.0, 1.0);
}
)";

struct PlaneSpec {
    uint32_t m_widthShift = 0;
    uint32_t m_heightShift = 0;
    uint32_t m_bytesPerPixel = 1;
    GLenum m_readFormat = GL_RED;
    char const* m_fragmentShader = nullptr;
};

struct FormatSpec {
    AVPixelFormat m_pixelFormat = AV_PIX_FMT_NONE;
    uint32_t m_planeCount = 0;
    std::array<PlaneSpec, 3> m_planes{};

    uint32_t planeWidth(uint32_t index, uint32_t alignedWidth) const {
        return alignedWidth >> m_planes[index].m_widthShift;
    }

    uint32_t planeHeight(uint32_t index, uint32_t alignedHeight) const {
        return alignedHeight >> m_planes[index].m_heightShift;
    }

    size_t planeSize(uint32_t index, uint32_t alignedWidth,
                     uint32_t alignedHeight) const {
        return static_cast<size_t>(planeWidth(index, alignedWidth)) *
               planeHeight(index, alignedHeight) *
               m_planes[index].m_bytesPerPixel;
    }

    size_t planeOffset(uint32_t index, uint32_t alignedWidth,
                       uint32_t alignedHeight) const {
        size_t offset = 0;
        for (uint32_t i = 0; i < index; ++i)
            offset += planeSize(i, alignedWidth, alignedHeight);
        return offset;
    }

    size_t bufferSize(uint32_t alignedWidth, uint32_t alignedHeight) const {
        return planeOffset(m_planeCount, alignedWidth, alignedHeight);
    }
};

inline constexpr FormatSpec FORMAT_NV12{
    .m_pixelFormat = AV_PIX_FMT_NV12,
    .m_planeCount = 2,
    .m_planes = {
        PlaneSpec{0, 0, 1, GL_RED, SHADER_Y},
        PlaneSpec{1, 1, 2, GL_RG, SHADER_UV},
    },
};

inline constexpr FormatSpec FORMAT_YUV420P{
    .m_pixelFormat = AV_PIX_FMT_YUV420P,
    .m_planeCount = 3,
    .m_planes = {
        PlaneSpec{0, 0, 1, GL_RED, SHADER_Y},
        PlaneSpec{1, 1, 1, GL_RED, SHADER_U},
        PlaneSpec{1, 1, 1, GL_RED, SHADER_V},
    },
};

inline constexpr FormatSpec FORMAT_RGB0{
    .m_pixelFormat = AV_PIX_FMT_RGB0,
    .m_planeCount = 1,
    .m_planes = {PlaneSpec{0, 0, 4, GL_RGBA, nullptr}},
};

inline constexpr FormatSpec FORMAT_RGB24{
    .m_pixelFormat = AV_PIX_FMT_RGB24,
    .m_planeCount = 1,
    .m_planes = {PlaneSpec{0, 0, 3, GL_RGB, nullptr}},
};

inline constexpr std::array<FormatSpec const*, 4> SUPPORTED_FORMATS{
    &FORMAT_YUV420P, &FORMAT_NV12, &FORMAT_RGB0, &FORMAT_RGB24};

inline FormatSpec const* formatFor(AVPixelFormat pixelFormat) {
    for (FormatSpec const* spec : SUPPORTED_FORMATS)
        if (spec->m_pixelFormat == pixelFormat) return spec;
    return nullptr;
}

}
