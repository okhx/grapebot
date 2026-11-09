#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "../gl.hpp"
#include "format.hpp"

namespace grape::render {

class GpuCapture {
   public:
    static constexpr int RING_SIZE = 4;

    struct Lease {
        uint8_t* m_data = nullptr;
        size_t m_size = 0;
        int m_slot = -1;
        int64_t m_pts = 0;

        bool valid() const { return m_data != nullptr && m_slot >= 0; }
    };

    geode::Result<> init(FormatSpec const* format, uint32_t width,
                         uint32_t height);
    void destroy();

    Lease capture(int64_t pts);

    void release(int slot);

    std::vector<Lease> drainPending();

    bool hasCapacity() const {
        if (!m_format || m_bufferSize == 0) return false;
        if (m_pendingCount >= RING_SIZE) return false;
        return !m_slots[m_writeIndex].m_leased.load(std::memory_order_acquire);
    }

    void displayPreview();

    void setFade(float amount) {
        m_fade = std::clamp(amount, 0.0f, 1.0f);
    }

    size_t bufferSize() const { return m_bufferSize; }
    FormatSpec const* format() const { return m_format; }

    int sceneFramebuffer() const { return m_oldFbo; }

    ~GpuCapture() { destroy(); }

   private:
    struct Plane {
        GLuint m_program = 0;
        GLuint m_fbo = 0;
        GLuint m_tex = 0;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        GLenum m_readFormat = GL_RED;
        size_t m_offset = 0;
        GLint m_uTexture = -1;
        GLint m_uTexelSize = -1;
        bool m_convert = false;
    };

    struct Slot {
        GLuint m_pbo = 0;
        GLsync m_fence = nullptr;
        int64_t m_pts = 0;
        bool m_mapped = false;
        std::atomic<bool> m_leased{false};
    };

    Lease leaseOldest();

    geode::Result<> buildSceneTarget();
    geode::Result<> buildFadeProgram();
    void drawFade();
    geode::Result<> buildPlane(Plane& plane, PlaneSpec const& spec);
    void renderScene();
    void convertPlanes();

    FormatSpec const* m_format = nullptr;
    std::vector<Plane> m_planes;

    GLuint m_sceneFbo = 0;
    GLuint m_sceneTex = 0;
    GLuint m_quadVao = 0;
    GLuint m_quadVbo = 0;
    GLuint m_fadeProgram = 0;
    GLint m_uFadeColor = -1;
    float m_fade = 0.0f;

    Slot m_slots[RING_SIZE];
    size_t m_bufferSize = 0;
    int m_writeIndex = 0;

    int m_pending[RING_SIZE] = {};
    int m_pendingHead = 0;
    int m_pendingCount = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_viewWidth = 0;
    uint32_t m_viewHeight = 0;

    int m_oldFbo = 0;
};

}
