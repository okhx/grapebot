#include "gpu_capture.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace cocos2d;

namespace grape::render {

#ifdef GEODE_IS_ANDROID

geode::Result<> GpuCapture::init(FormatSpec const*, uint32_t, uint32_t) {
    return geode::Err("GPU capture is not available on Android");
}
void GpuCapture::destroy() {}
GpuCapture::Lease GpuCapture::capture(int64_t) { return {}; }
GpuCapture::Lease GpuCapture::leaseOldest() { return {}; }
std::vector<GpuCapture::Lease> GpuCapture::drainPending() { return {}; }
void GpuCapture::release(int) {}
void GpuCapture::displayPreview() {}
geode::Result<> GpuCapture::buildSceneTarget() { return geode::Ok(); }
geode::Result<> GpuCapture::buildPlane(Plane&, PlaneSpec const&) {
    return geode::Ok();
}
void GpuCapture::renderScene() {}
void GpuCapture::convertPlanes() {}
geode::Result<> GpuCapture::buildFadeProgram() { return geode::Ok(); }
void GpuCapture::drawFade() {}

#else

namespace {

void resizeSceneTo(CCSize size) {
    auto* director = CCDirector::sharedDirector();
    auto* view = CCEGLView::sharedOpenGLView();
    view->CCEGLViewProtocol::setFrameSize(size.width, size.height);
    director->updateScreenScale(size);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(0, 0, static_cast<GLsizei>(size.width),
               static_cast<GLsizei>(size.height));
}

void rescaleBackgrounds() {
    auto* layer = GJBaseGameLayer::get();
    if (!layer) return;

    const auto win = CCDirector::sharedDirector()->getWinSize();
    for (auto* child :
         geode::cocos::CCArrayExt<CCNode*>(layer->getChildren())) {
        auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(child);
        if (!sprite) continue;

        const auto content = sprite->getContentSize();
        if (content.width < 150.f || content.height < 100.f) continue;
        const float ratio = content.width / content.height;
        if (ratio < 0.4f || ratio > 4.f) continue;

        sprite->setScaleX(win.width / content.width);
        sprite->setScaleY(win.height / content.height);
    }
}

geode::Result<GLuint> compile(GLenum type, char const* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        glDeleteShader(shader);
        return geode::Err("Shader compilation failed: {}", log);
    }
    return geode::Ok(shader);
}

geode::Result<GLuint> linkProgram(char const* vertex, char const* fragment) {
    GEODE_UNWRAP_INTO(GLuint vs, compile(GL_VERTEX_SHADER, vertex));
    auto fsResult = compile(GL_FRAGMENT_SHADER, fragment);
    if (fsResult.isErr()) {
        glDeleteShader(vs);
        return geode::Err(fsResult.unwrapErr());
    }
    const GLuint fs = fsResult.unwrap();

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_texCoord");
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        glDeleteProgram(program);
        return geode::Err("Shader link failed: {}", log);
    }
    return geode::Ok(program);
}

geode::Result<> attachColorTarget(GLuint fbo, GLuint tex, uint32_t width,
                                  uint32_t height, GLint internalFormat,
                                  GLenum format) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, format, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        return geode::Err("Framebuffer incomplete: 0x{:x}", status);
    return geode::Ok();
}

}

geode::Result<> GpuCapture::buildSceneTarget() {
    glGenFramebuffers(1, &m_sceneFbo);
    glGenTextures(1, &m_sceneTex);
    return attachColorTarget(m_sceneFbo, m_sceneTex, m_width, m_height, GL_RGBA,
                             GL_RGBA);
}

geode::Result<> GpuCapture::buildFadeProgram() {
    GEODE_UNWRAP_INTO(m_fadeProgram,
                      linkProgram(PLANE_VERTEX_SHADER, FADE_FRAGMENT_SHADER));
    m_uFadeColor = glGetUniformLocation(m_fadeProgram, "u_color");
    return geode::Ok();
}

void GpuCapture::drawFade() {
    if (m_fade <= 0.0f || !m_fadeProgram || !m_quadVao) return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, static_cast<GLsizei>(m_width),
               static_cast<GLsizei>(m_height));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_fadeProgram);
    if (m_uFadeColor >= 0) glUniform4f(m_uFadeColor, 0.f, 0.f, 0.f, m_fade);
    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

geode::Result<> GpuCapture::buildPlane(Plane& plane, PlaneSpec const& spec) {
    if (!plane.m_convert) return geode::Ok();

    glGenFramebuffers(1, &plane.m_fbo);
    glGenTextures(1, &plane.m_tex);

    const GLint internalFormat = spec.m_bytesPerPixel == 1   ? GL_R8
                                 : spec.m_bytesPerPixel == 2 ? GL_RG8
                                                             : GL_RGBA8;
    const GLenum format = spec.m_bytesPerPixel == 1   ? GL_RED
                          : spec.m_bytesPerPixel == 2 ? GL_RG
                                                      : GL_RGBA;

    GEODE_UNWRAP(attachColorTarget(plane.m_fbo, plane.m_tex, plane.m_width,
                                   plane.m_height, internalFormat, format));

    GEODE_UNWRAP_INTO(plane.m_program,
                      linkProgram(PLANE_VERTEX_SHADER, spec.m_fragmentShader));
    plane.m_uTexture = glGetUniformLocation(plane.m_program, "u_texture");
    plane.m_uTexelSize = glGetUniformLocation(plane.m_program, "u_texelSize");
    return geode::Ok();
}

geode::Result<> GpuCapture::init(FormatSpec const* format, uint32_t width,
                                 uint32_t height) {
    destroy();

    if (!format) return geode::Err("No pixel format supplied");
    if (width == 0 || height == 0)
        return geode::Err("Invalid capture size {}x{}", width, height);

    m_format = format;
    m_width = width;
    m_height = height;
    m_bufferSize = format->bufferSize(width, height);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFbo);

    GEODE_UNWRAP(buildSceneTarget());
    GEODE_UNWRAP(buildFadeProgram());
    m_fade = 0.0f;

    m_planes.resize(format->m_planeCount);
    for (uint32_t i = 0; i < format->m_planeCount; ++i) {
        auto const& spec = format->m_planes[i];
        Plane& plane = m_planes[i];
        plane.m_width = format->planeWidth(i, width);
        plane.m_height = format->planeHeight(i, height);
        plane.m_readFormat = spec.m_readFormat;
        plane.m_offset = format->planeOffset(i, width, height);
        plane.m_convert = spec.m_fragmentShader != nullptr;
        GEODE_UNWRAP(buildPlane(plane, spec));
    }

    for (auto& slot : m_slots) {
        glGenBuffers(1, &slot.m_pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.m_pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER,
                     static_cast<GLsizeiptr>(m_bufferSize), nullptr,
                     GL_STREAM_READ);
        slot.m_mapped = false;
        slot.m_leased.store(false, std::memory_order_relaxed);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    static constexpr float QUAD[] = {
        -1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f,
        1.f,  1.f,  1.f, 1.f, -1.f, 1.f, 0.f, 1.f,
    };
    glGenVertexArrays(1, &m_quadVao);
    glBindVertexArray(m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    m_writeIndex = 0;
    m_pendingHead = 0;
    m_pendingCount = 0;

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFbo);
    return geode::Ok();
}

void GpuCapture::destroy() {
    for (auto& slot : m_slots) {
        if (slot.m_mapped) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.m_pbo);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            slot.m_mapped = false;
        }
        if (slot.m_fence) {
            glDeleteSync(slot.m_fence);
            slot.m_fence = nullptr;
        }
        if (slot.m_pbo) {
            glDeleteBuffers(1, &slot.m_pbo);
            slot.m_pbo = 0;
        }
        slot.m_leased.store(false, std::memory_order_relaxed);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    for (auto& plane : m_planes) {
        if (plane.m_program) glDeleteProgram(plane.m_program);
        if (plane.m_fbo) glDeleteFramebuffers(1, &plane.m_fbo);
        if (plane.m_tex) glDeleteTextures(1, &plane.m_tex);
    }
    m_planes.clear();

    if (m_sceneFbo) glDeleteFramebuffers(1, &m_sceneFbo);
    if (m_sceneTex) glDeleteTextures(1, &m_sceneTex);
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_quadVao) glDeleteVertexArrays(1, &m_quadVao);
    if (m_fadeProgram) glDeleteProgram(m_fadeProgram);
    m_sceneFbo = m_sceneTex = m_quadVbo = m_quadVao = m_fadeProgram = 0;
    m_uFadeColor = -1;

    m_format = nullptr;
    m_bufferSize = 0;
    m_writeIndex = 0;
    m_pendingHead = 0;
    m_pendingCount = 0;
}

void GpuCapture::renderScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, static_cast<GLsizei>(m_width),
               static_cast<GLsizei>(m_height));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    resizeSceneTo(CCSize(static_cast<float>(m_width),
                         static_cast<float>(m_height)));
    rescaleBackgrounds();

    if (auto* scene = CCDirector::get()->m_pRunningScene) scene->visit();

    drawFade();
}

void GpuCapture::convertPlanes() {
    glDisable(GL_BLEND);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    for (auto const& plane : m_planes) {
        if (plane.m_convert) {
            glBindFramebuffer(GL_FRAMEBUFFER, plane.m_fbo);
            glViewport(0, 0, static_cast<GLsizei>(plane.m_width),
                       static_cast<GLsizei>(plane.m_height));

            glUseProgram(plane.m_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_sceneTex);
            glUniform1i(plane.m_uTexture, 0);
            if (plane.m_uTexelSize >= 0)
                glUniform2f(plane.m_uTexelSize, 1.f / plane.m_width,
                            1.f / plane.m_height);

            glBindVertexArray(m_quadVao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glBindVertexArray(0);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
            glViewport(0, 0, static_cast<GLsizei>(plane.m_width),
                       static_cast<GLsizei>(plane.m_height));
        }

        glReadPixels(0, 0, static_cast<GLsizei>(plane.m_width),
                     static_cast<GLsizei>(plane.m_height), plane.m_readFormat,
                     GL_UNSIGNED_BYTE,
                     reinterpret_cast<void*>(plane.m_offset));
    }
}

GpuCapture::Lease GpuCapture::leaseOldest() {
    if (m_pendingCount == 0) return {};

    const int index = m_pending[m_pendingHead];
    Slot& ready = m_slots[index];

    bool signalled = ready.m_fence == nullptr;
    if (ready.m_fence) {
        const GLenum status =
            glClientWaitSync(ready.m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        signalled =
            status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
    }
    if (!signalled) return {};

    glBindBuffer(GL_PIXEL_PACK_BUFFER, ready.m_pbo);
    auto* pixels = static_cast<uint8_t*>(
        glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                         static_cast<GLsizeiptr>(m_bufferSize), GL_MAP_READ_BIT));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_pendingHead = (m_pendingHead + 1) % RING_SIZE;
    --m_pendingCount;

    if (!pixels) {
        geode::log::error("Failed to map readback buffer: {}", glGetError());
        return {};
    }

    ready.m_mapped = true;
    ready.m_leased.store(true, std::memory_order_release);
    return Lease{pixels, m_bufferSize, index, ready.m_pts};
}

GpuCapture::Lease GpuCapture::capture(int64_t pts) {
    if (!hasCapacity()) return {};

    Slot& write = m_slots[m_writeIndex];
    write.m_pts = pts;

    auto* director = CCDirector::sharedDirector();
    const CCSize viewSize = director->getOpenGLView()->getFrameSize();
    m_viewWidth = static_cast<uint32_t>(viewSize.width);
    m_viewHeight = static_cast<uint32_t>(viewSize.height);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, write.m_pbo);
    if (write.m_mapped) {
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        write.m_mapped = false;
    }
    if (write.m_fence) {
        glDeleteSync(write.m_fence);
        write.m_fence = nullptr;
    }

    renderScene();
    convertPlanes();

    write.m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    m_pending[(m_pendingHead + m_pendingCount) % RING_SIZE] = m_writeIndex;
    ++m_pendingCount;
    m_writeIndex = (m_writeIndex + 1) % RING_SIZE;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    const Lease lease = leaseOldest();

    resizeSceneTo(CCSize(static_cast<float>(m_viewWidth),
                         static_cast<float>(m_viewHeight)));
    rescaleBackgrounds();
    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFbo);
    glUseProgram(0);
    glEnable(GL_BLEND);

    return lease;
}

std::vector<GpuCapture::Lease> GpuCapture::drainPending() {
    std::vector<Lease> leases;
    if (m_pendingCount == 0) return leases;

    glFinish();

    const int queued = m_pendingCount;
    for (int i = 0; i < queued; ++i) {
        const Lease lease = leaseOldest();
        if (lease.valid()) leases.push_back(lease);
    }
    return leases;
}

void GpuCapture::release(int slot) {
    if (slot < 0 || slot >= RING_SIZE) return;
    m_slots[slot].m_leased.store(false, std::memory_order_release);
}

void GpuCapture::displayPreview() {
    if (!m_sceneFbo) return;

    const CCSize size =
        CCDirector::sharedDirector()->getOpenGLView()->getFrameSize();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_oldFbo));
    glBlitFramebuffer(0, 0, static_cast<GLint>(m_width),
                      static_cast<GLint>(m_height), 0, 0,
                      static_cast<GLint>(size.width),
                      static_cast<GLint>(size.height), GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFbo);
}

#endif

}
