#pragma once

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif


#ifndef IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include "render/gl.hpp"
#ifdef GEODE_IS_MACOS
#include "backends/imgui_impl_opengl2.h"
#else
#include "backends/imgui_impl_opengl3.h"
#endif
#ifdef GEODE_IS_WINDOWS
#include "backends/imgui_impl_win32.h"
#endif

#include "engine/engine.hpp"
#include "imgui.h"
#include "render/pass.hpp"
#include "theme.hpp"
#include "ui/manager.hpp"

#ifdef GEODE_IS_WINDOWS
LRESULT CALLBACK h_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif

class ImGuiHookCtx {
   private:
    ImGuiHookCtx() = default;

    bool m_inited = false;

   public:
    constexpr static float BLUR_DOWNSCALING_FACTOR = 3.0f;

#ifdef GEODE_IS_WINDOWS
    HWND m_hWnd = nullptr;
#endif
    GLuint m_inputTex;
    GLint m_oldFbo;
    GLint m_oldProgram;
    GLint m_oldTex;
    RenderPass m_blurPass;

    float m_width;
    float m_height;
    float m_time;

#ifdef GEODE_IS_WINDOWS
    WNDPROC m_oWndProc = nullptr;
#endif
    ImVec4 m_windowPos;

    static ImGuiHookCtx& get() {
        static ImGuiHookCtx ctx;
        return ctx;
    }

    struct RenderData {
        cocos2d::CCSize m_size;
        cocos2d::CCPoint m_pos;
    };

    RenderData m_renderData;
    void init(cocos2d::CCEGLView* view);

    void preSampleBlur(ImVec4 windowPos);
    void sampleBlurFirstPass();
    void sampleBlurSecondPass();

    void sampleBlurPostprocess();

    void postSampleBlur();

    void handleResize(float width, float height);

    void draw();
};
