#include "hook.hpp"
#include "hooks/frame_fallback.hpp"

#include <algorithm>
#include <cstring>

#ifdef GEODE_IS_WINDOWS
#include <winuser.h>
#include <windowsx.h>
#include "Geode/cocos/platform/win32/CCGL.h"
#include "backends/imgui_impl_win32.h"
#endif

#include "Geode/cocos/CCDirector.h"
#ifdef GEODE_IS_MACOS
#include "backends/imgui_impl_opengl2.h"
#else
#include "backends/imgui_impl_opengl3.h"
#endif
#include "imgui.h"
#include "render/renderer.hpp"
#include "vine/session.hpp"
#include "ui/manager.hpp"
#include "ui/notifications.hpp"
#include "util/storage.hpp"

#ifdef GEODE_IS_MACOS
#include "ui/macos_input.hpp"
#endif

using namespace geode::prelude;

#ifdef GEODE_IS_MACOS



static void macosKeybindHandler(int key, bool pressed, bool ctrl, bool shift,
                                bool alt) {
    auto* bindings = BindingManager::get();
    if (bindings->isWaitingForKey()) {
        if (pressed) {
            bindings->setNewKey(static_cast<cocos2d::enumKeyCodes>(key), ctrl,
                               shift, alt);
        }
        return;
    }
    
    
    if (!pressed) {
        bindings->processKeyEvent(key, false, ctrl, shift, alt);
        return;
    }
    
    if (ImGui::GetIO().WantTextInput) return;
    bindings->processKeyEvent(key, true, ctrl, shift, alt);
}



static bool glSupportsPrograms() {
    static const bool supported = [] {
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (!version) return false;
        return std::strncmp(version, "2.0", 3) != 0;
    }();
    return supported;
}
#endif

#ifdef GEODE_IS_WINDOWS
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#ifdef GEODE_IS_WINDOWS
constexpr int KEY_MW_UP = 0x97;
constexpr int KEY_MW_DOWN = 0x98;

LRESULT CALLBACK h_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    bool shiftHeld = GetKeyState(VK_SHIFT) & 0x8000;
    bool ctrlHeld = GetKeyState(VK_CONTROL) & 0x8000;
    bool altHeld = GetKeyState(VK_MENU) & 0x8000;

    if (uMsg == WM_SIZE) {
        ImGuiHookCtx::get().handleResize(LOWORD(lParam), HIWORD(lParam));
    }

    if (uMsg == WM_SIZING) {
        ImGuiHookCtx::get().handleResize(LOWORD(lParam), HIWORD(lParam));
    }

    if (uMsg == WM_LBUTTONDOWN &&
        grape::ui::NotificationCenter::get().dismissAt(
            static_cast<float>(GET_X_LPARAM(lParam)),
            static_cast<float>(GET_Y_LPARAM(lParam))))
        return 0;

    if ((uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR) &&
        CCIMEDispatcher::sharedDispatcher()->hasDelegate() &&
        !ImGui::GetIO().WantCaptureKeyboard) {
        if (uMsg == WM_KEYUP) {
            BindingManager::get()->processKeyEvent(
                static_cast<int>(wParam), false, ctrlHeld, shiftHeld, altHeld);
        }
        return CallWindowProc(ImGuiHookCtx::get().m_oWndProc, hWnd, uMsg,
                              wParam, lParam);
    }

    if (GrapeEngine::get()->ui().m_state.m_visible->inner()) {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYUP ||
            uMsg == WM_SYSKEYUP || uMsg == WM_CHAR) {
            if (ImGui::GetIO().WantCaptureKeyboard) {
                if (!ImGui::GetIO().WantTextInput &&
                    (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN ||
                     uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)) {
                    BindingManager::get()->processKeyEvent(
                        static_cast<int>(wParam),
                        uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN,
                        ctrlHeld, shiftHeld, altHeld);
                }
                return true;
            }
        }

        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN ||
            uMsg == WM_MBUTTONDOWN || uMsg == WM_MOUSEWHEEL) {
            int key = wParam;

            if (uMsg == WM_MOUSEWHEEL) {
                if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
                    key = KEY_MW_UP;
                } else {
                    key = KEY_MW_DOWN;
                }
            }

            if (BindingManager::get()->isWaitingForKey()) {
                BindingManager::get()->setNewKey(
                    static_cast<cocos2d::enumKeyCodes>(key), ctrlHeld,
                    shiftHeld, altHeld);
                return true;
            }

            BindingManager::get()->processKeyEvent(key, true, ctrlHeld,
                                                    shiftHeld, altHeld);
            if (uMsg == WM_MOUSEWHEEL) {
                BindingManager::get()->processKeyEvent(
                    key, false, ctrlHeld, shiftHeld, altHeld);
            }
        } else if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP ||
                   uMsg == WM_MBUTTONUP) {
            int key = wParam;
            BindingManager::get()->processKeyEvent(key, false, ctrlHeld,
                                                    shiftHeld, altHeld);
        }

        if (ImGui::GetIO().WantCaptureMouse) {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                return true;
            }
        }
    } else {
        if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN ||
            uMsg == WM_MBUTTONDOWN || uMsg == WM_MOUSEWHEEL) {
            int key = wParam;

            if (uMsg == WM_MOUSEWHEEL) {
                if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
                    key = KEY_MW_UP;
                } else {
                    key = KEY_MW_DOWN;
                }
            }

            BindingManager::get()->processKeyEvent(key, true, ctrlHeld,
                                                    shiftHeld, altHeld);
            if (uMsg == WM_MOUSEWHEEL) {
                BindingManager::get()->processKeyEvent(
                    key, false, ctrlHeld, shiftHeld, altHeld);
            }
        } else if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP ||
                   uMsg == WM_MBUTTONUP) {
            int key = wParam;
            BindingManager::get()->processKeyEvent(key, false, ctrlHeld,
                                                    shiftHeld, altHeld);
        }
    }

    if (uMsg == WM_DROPFILES) {
        const auto drop = reinterpret_cast<HDROP>(wParam);

        TCHAR fileName[256];
        const UINT numFiles = DragQueryFile(drop, 0xFFFFFFFF, nullptr, 0);

        for (UINT i = 0; i < numFiles; i++) {
            auto& rs = GrapeEngine::get()->replay();

            DragQueryFile(drop, i, fileName, 256);
            std::filesystem::path path(fileName);
            geode::log::info("Loading file via drag-and-drop: {}", path);
            if (path.extension() != ".vine" &&
                path.extension() != ".grape" &&
                path.extension() != ".slc") {
                continue;
            }

            std::filesystem::path dest = grape::paths::directory("replays") /
                                     (path.stem().string() + ".vine");

            rs.createBackup();
            rs.backupExisting(dest);
            rs.load(path);
            rs.save(dest);

            rs.m_replayName = path.stem().string();
        }

        DragFinish(drop);
    }

    return CallWindowProc(ImGuiHookCtx::get().m_oWndProc, hWnd, uMsg, wParam,
                          lParam);
}
#endif

#ifndef GEODE_IS_MOBILE
const char* BLUR_VERT = R"(#version 130
#extension GL_ARB_explicit_attrib_location : require

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_texCoord;

out vec2 v_texCoord;

void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";

static const char* BLUR_FRAG = R"(#version 130
#extension GL_ARB_explicit_attrib_location : require
#extension GL_ARB_explicit_uniform_location : require

in vec2 v_texCoord;
out vec4 fragColor;

layout(location = 0) uniform sampler2D u_texture;
layout(location = 1) uniform vec2 u_texelSize;
layout(location = 2) uniform vec2 u_direction;
layout(location = 3) uniform vec4 u_window;

vec4 sampleBlur(vec2 texCoord) {
    vec4 color = vec4(0.0);
    vec2 off1 = vec2(1.3846153846) * u_direction * u_texelSize;
    vec2 off2 = vec2(4.2307692308) * u_direction * u_texelSize;
    color += texture2D(u_texture, texCoord) * 0.2270270270;
    color += texture2D(u_texture, texCoord + off1) * 0.3162162162;
    color += texture2D(u_texture, texCoord - off1) * 0.3162162162;
    color += texture2D(u_texture, texCoord + off2) * 0.0702702703;
    color += texture2D(u_texture, texCoord - off2) * 0.0702702703;

    return color;
}

vec4 sampleReflection(vec2 texCoord, float displacement) {
    vec2 windowCenter = vec2(
        (u_window.x + u_window.z) / 2.0,
        (u_window.y + u_window.w) / 2.0
    );

    vec2 normal = windowCenter - texCoord;
    vec2 reflectPos = texCoord + (normal * displacement);

    vec2 sunDirection = normalize(vec2(0.4, -1.0));
    float specular = 0.0;

    vec4 reflected = texture2D(u_texture, reflectPos);
    return mix(vec4(1.0, 1.0, 1.0, 1.0), vec4(reflected.xyz, 1.0), 1.0 - specular);
}

float roundedRectSDF(vec2 point, vec2 center, vec2 halfSize, float radius) {
    vec2 q = abs(point - center) - halfSize + radius;

    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

vec4 sampleColor(vec2 texCoord) {
    vec4 finalColor = vec4(0.0);
    vec4 regularColor = vec4(texture2D(u_texture, texCoord).xyz, 1.0);
    vec4 blurColor = vec4(sampleBlur(texCoord).xyz, 1.0);

    float MAX_DISTANCE = 0.05;

    vec2 center = (u_window.xy + u_window.zw) * 0.5;
    vec2 halfSize = (u_window.zw - u_window.xy) * 0.5;
    float radius = 0.06;

    float dst = 1.0 + roundedRectSDF(texCoord, center, halfSize, radius);
    dst = dst - (1.0 - MAX_DISTANCE);
    dst = dst / MAX_DISTANCE;

    float intensity = clamp(dst, 0.0, 1.0);

    vec4 reflectedColor = sampleReflection(texCoord, intensity * 0.7);
    reflectedColor = mix(regularColor, reflectedColor, intensity - 0.3);

    finalColor = mix(blurColor, reflectedColor, clamp(intensity, 0.0, 0.3));
    return finalColor;
}

void main() {
    fragColor = sampleColor(v_texCoord);
}
)";
#endif

void ImGuiHookCtx::init(cocos2d::CCEGLView* view) {
#ifdef GEODE_IS_MOBILE
    (void)view;
    return;
#endif

#ifdef GEODE_IS_WINDOWS
    if (m_inited) {
        if (auto* current = view->getWindow()) {
            HDC hdc = *reinterpret_cast<HDC*>(
                reinterpret_cast<uintptr_t>(current) + 632);
            if (WindowFromDC(hdc) != m_hWnd) {
                if (GetWindowLongPtr(m_hWnd, GWLP_WNDPROC) ==
                    reinterpret_cast<LONG_PTR>(h_WndProc)) {
                    SetWindowLongPtr(m_hWnd, GWLP_WNDPROC,
                                     reinterpret_cast<LONG_PTR>(m_oWndProc));
                }
                ImGui_ImplWin32_Shutdown();
                ImGui_ImplOpenGL3_Shutdown();
                m_inited = false;
            }
        }
        if (m_inited) {
            return;
        }
    }
#else
    if (m_inited) {
        return;
    }
#endif

    m_time = 0.0f;

    static bool listenersBound = false;
    if (!listenersBound) {
        listenersBound = true;
        auto mouse = MouseInputEvent().listen([](MouseInputData&) {
            
            
            
            
            if (!GrapeEngine::get()->ui().m_state.m_visible->inner()) {
                return ListenerResult::Propagate;
            }
            
            
            
            
            const bool hovered = ImGui::GetIO().WantCaptureMouse ||
                                 ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
#ifdef GEODE_IS_MACOS
            static bool lastStop = false;
            if (hovered != lastStop) {
                lastStop = hovered;
                geode::log::info("[grape-mouse] game-click stop={} hoveredAny={}",
                                 hovered, hovered);
            }
#endif
            if (hovered) {
                return ListenerResult::Stop;
            }

            return ListenerResult::Propagate;
        });
        mouse.leak();

        auto kb = KeyboardInputEvent().listen([](KeyboardInputData&) {
            
            
            
            
            if (ImGui::GetIO().WantCaptureKeyboard) {
                return ListenerResult::Stop;
            }

            return ListenerResult::Propagate;
        });
        kb.leak();
    }

#ifdef GEODE_IS_WINDOWS
    auto* glfwWindow = view->getWindow();
    HDC hdc =
        *reinterpret_cast<HDC*>(reinterpret_cast<uintptr_t>(glfwWindow) + 632);
    m_hWnd = WindowFromDC(hdc);

    DragAcceptFiles(m_hWnd, TRUE);

    m_oWndProc =
        (WNDPROC)SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
#endif

    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
    }
    ImGui::GetIO().IniFilename = nullptr;
    
#ifdef GEODE_IS_WINDOWS
    ImGui_ImplWin32_InitForOpenGL(m_hWnd);
#endif
#ifdef GEODE_IS_MACOS
    
    
    
    grape::ui::macosInputInit();
#endif
#ifdef GEODE_IS_MOBILE
    ImGui_ImplOpenGL3_Init("#version 100");
#elif defined(GEODE_IS_MACOS)
    ImGui_ImplOpenGL2_Init();
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    float width = view->getFrameSize().width / BLUR_DOWNSCALING_FACTOR;
    float height = view->getFrameSize().height / BLUR_DOWNSCALING_FACTOR;

    m_width = width * BLUR_DOWNSCALING_FACTOR;
    m_height = height * BLUR_DOWNSCALING_FACTOR;

    GrapeEngine::get()->ui().setup();

#if !defined(GEODE_IS_MOBILE) && !defined(GEODE_IS_MACOS)
    m_blurPass = RenderPass{.m_width = (unsigned int)width,
                            .m_height = (unsigned int)height,
                            .m_vertexShader = BLUR_VERT,
                            .m_fragmentShader = BLUR_FRAG};

    GLint oldFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);

    m_blurPass.initialize();

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &m_inputTex);
    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_blurPass.m_width,
                 m_blurPass.m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_oldTex);

    glBindFramebuffer(GL_FRAMEBUFFER, m_blurPass.m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           m_inputTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
#endif

    geode::log::info("Initialized ImGUI");
    m_inited = true;
}

void ImGuiHookCtx::handleResize(float width, float height) {
    if (!m_inited) return;

    width /= BLUR_DOWNSCALING_FACTOR;
    height /= BLUR_DOWNSCALING_FACTOR;

    m_width = width * BLUR_DOWNSCALING_FACTOR;
    m_height = height * BLUR_DOWNSCALING_FACTOR;

#if !defined(GEODE_IS_MOBILE) && !defined(GEODE_IS_MACOS)
    m_blurPass.m_width = (unsigned int)width;
    m_blurPass.m_height = (unsigned int)height;
    m_blurPass.resize();

    GrapeEngine::get()->ui().m_theme->resize(width, height);

    glBindTexture(GL_TEXTURE_2D, m_inputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_blurPass.m_width,
                 m_blurPass.m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#endif
}

#if defined(GEODE_IS_MOBILE) || defined(GEODE_IS_MACOS)
void ImGuiHookCtx::preSampleBlur(ImVec4) {}
void ImGuiHookCtx::sampleBlurFirstPass() {}
void ImGuiHookCtx::sampleBlurSecondPass() {}
void ImGuiHookCtx::sampleBlurPostprocess() {}
void ImGuiHookCtx::postSampleBlur() {}
#else
void ImGuiHookCtx::preSampleBlur(ImVec4 window) {
    if (!m_inited) return;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_oldFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_oldProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_oldTex);

    glViewport(0, 0, m_blurPass.m_width, m_blurPass.m_height);
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blurPass.m_fbo);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_blurPass.m_tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           m_inputTex, 0);

    glReadBuffer(GL_BACK);

    if (auto renderer = Renderer::get(); renderer->isRecording()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
                          renderer->sceneFramebuffer());
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_blurPass.m_width,
                      m_blurPass.m_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, false, 20, 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, false, 20, (void*)8);

    m_windowPos = window;
}

void ImGuiHookCtx::sampleBlurFirstPass() {
    if (!m_inited) return;

    glUseProgram(m_blurPass.m_program);

    glUniform1i(0, 0);
    glUniform2f(1, 1.0f / m_blurPass.m_width, 1.0f / m_blurPass.m_height);
    float blurExp = 1.6f;
    glUniform2f(2, blurExp, 0.0f);
    glUniform4f(3, m_windowPos.x, m_windowPos.y, m_windowPos.z, m_windowPos.w);

    glDrawBuffer(GL_COLOR_ATTACHMENT1);
}

void ImGuiHookCtx::sampleBlurSecondPass() {
    if (!m_inited) return;

    glUseProgram(m_blurPass.m_program);

    glUniform1i(0, 0);
    glUniform2f(1, 1.0f / m_blurPass.m_width, 1.0f / m_blurPass.m_height);
    float blurExp = 1.6f;
    glUniform2f(2, 0.0f, blurExp);
    glUniform4f(3, m_windowPos.x, m_windowPos.y, m_windowPos.z, m_windowPos.w);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void ImGuiHookCtx::sampleBlurPostprocess() {
    if (!m_inited) return;

    glUseProgram(GrapeEngine::get()->ui().m_theme->m_postprocessPass.m_program);

    glUniform1i(0, 0);
    glUniform2f(1, 1.0f / m_blurPass.m_width, 1.0f / m_blurPass.m_height);
    float blurExp = 1.6f;
    glUniform2f(2, blurExp, 0.0f);
    glUniform4f(3, m_windowPos.x, m_windowPos.y, m_windowPos.z, m_windowPos.w);
    glUniform1f(4, m_time);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void ImGuiHookCtx::postSampleBlur() {
    cocos2d::CCSize frameSize =
        CCDirector::sharedDirector()->getOpenGLView()->getFrameSize();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_oldFbo);
    glUseProgram(m_oldProgram);
    glBindTexture(GL_TEXTURE_2D, m_oldTex);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_PRIMITIVE_RESTART);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, frameSize.width, frameSize.height);
}
#endif

void ImGuiHookCtx::draw() {
#ifdef GEODE_IS_MOBILE
    return;
#endif

    init(CCEGLView::get());

#ifndef GEODE_IS_WINDOWS
    auto& io = ImGui::GetIO();
    const auto frameSize = CCEGLView::get()->getFrameSize();
    io.DisplaySize = ImVec2(frameSize.width, frameSize.height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = std::max(CCDirector::get()->getDeltaTime(), 1.0f / 1000.0f);
#endif

#ifdef GEODE_IS_MACOS
    grape::ui::macosInputInit();
    grape::ui::macosInputSetKeyHandler(&macosKeybindHandler);
    ImGui_ImplOpenGL2_NewFrame();
    grape::ui::macosInputNewFrame();
    {
        
        
        
        
        GLint viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (viewport[2] > 0 && viewport[3] > 0 && io.DisplaySize.x > 0.0f &&
            io.DisplaySize.y > 0.0f) {
            io.DisplayFramebufferScale = ImVec2(
                viewport[2] / io.DisplaySize.x, viewport[3] / io.DisplaySize.y);
        }
    }
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
#ifdef GEODE_IS_WINDOWS
    ImGui_ImplWin32_NewFrame();
#endif
    ImGui::NewFrame();
    GrapeEngine::get()->ui().draw();
    ImGui::Render();
#ifdef GEODE_IS_MACOS
    
    
    
    
    
    
    
    
    
    GLint lastProgram = 0, lastArrayBuffer = 0, lastElementBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementBuffer);
    const bool programSupport = glSupportsPrograms();
    if (programSupport) glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(lastElementBuffer));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(lastArrayBuffer));
    if (programSupport) glUseProgram(static_cast<GLuint>(lastProgram));
#else
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

struct GrapeEGLView : Modify<GrapeEGLView, CCEGLView> {
    void swapBuffers() {
#ifdef GEODE_IS_MOBILE
        CCEGLView::swapBuffers();
#else
        if (grape::frame::beforeSwap()) {
            ImGuiHookCtx::get().draw();
            CCEGLView::swapBuffers();
        }
        grape::frame::afterSwap();
#endif
    }
};
