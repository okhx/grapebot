#include "macos_input.hpp"






#import <Cocoa/Cocoa.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <CoreGraphics/CoreGraphics.h>

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_osx.h"



ImGuiKey ImGui_ImplOSX_KeyCodeToImGuiKey(int key_code);

namespace grape::ui {

namespace {

NSView* g_view = nil;
bool g_imguiReady = false;
id g_keyMonitor = nil;
MacOSKeyHandler g_keyHandler = nullptr;




struct HeldKey {
    int key;
    unsigned short keyCode;
    
    
    int upFrames = 0;
};
std::vector<HeldKey> g_heldKeys;




struct KeyEvent {
    int key;
    bool down;
    bool ctrl;
    bool shift;
    bool alt;
};
std::mutex g_inputMutex;
std::vector<KeyEvent> g_keyQueue;
std::string g_charQueue;






int canonicalKeyCode(NSEvent* event) {
    switch (event.keyCode) {
        case 0x24: return 0x0D; 
        case 0x30: return 0x09; 
        case 0x31: return 0x20; 
        case 0x33: return 0x08; 
        case 0x35: return 0x1B; 
        case 0x7A: return 0x70; 
        case 0x78: return 0x71; 
        case 0x63: return 0x72; 
        case 0x76: return 0x73; 
        case 0x60: return 0x74; 
        case 0x61: return 0x75; 
        case 0x62: return 0x76; 
        case 0x64: return 0x77; 
        case 0x65: return 0x78; 
        case 0x6D: return 0x79; 
        case 0x67: return 0x7A; 
        case 0x6F: return 0x7B; 
        case 0x73: return 0x24; 
        case 0x74: return 0x21; 
        case 0x75: return 0x2E; 
        case 0x77: return 0x23; 
        case 0x79: return 0x22; 
        case 0x7B: return 0x25; 
        case 0x7C: return 0x27; 
        case 0x7D: return 0x28; 
        case 0x7E: return 0x26; 
        default: break;
    }

    
    
    NSString* characters = [event charactersIgnoringModifiers];
    if (characters.length == 0) return 0;
    const unichar character = [characters characterAtIndex:0];
    if (character >= 'a' && character <= 'z') return 'A' + (character - 'a');
    if (character >= 'A' && character <= 'Z') return character;
    if (character >= '0' && character <= '9') return character;
    return 0;
}

NSEvent* keyMonitor(NSEvent* event) {
    if (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)
        return event;

    const NSEventModifierFlags flags = [event modifierFlags];
    const bool ctrl = (flags & NSEventModifierFlagControl) != 0;
    const bool shift = (flags & NSEventModifierFlagShift) != 0;
    const bool alt = (flags & NSEventModifierFlagOption) != 0;
    const bool command = (flags & NSEventModifierFlagCommand) != 0;
    const bool down = event.type == NSEventTypeKeyDown;
    const bool repeat = down && event.isARepeat;
    const int key = canonicalKeyCode(event);

    
    
    
    
    if (!command) {
        std::lock_guard<std::mutex> lock(g_inputMutex);
        if (down && !repeat) {
            bool tracked = false;
            for (const HeldKey& held : g_heldKeys)
                tracked |= held.keyCode == event.keyCode;
            if (!tracked) g_heldKeys.push_back({key, event.keyCode});
        } else if (!down) {
            std::erase_if(g_heldKeys, [&](const HeldKey& held) {
                return held.keyCode == event.keyCode;
            });
        }
        if (g_keyHandler && key != 0 && !repeat)
            g_keyQueue.push_back({key, down, ctrl, shift, alt});
    }

    
    
    
    if (down && !command && !ctrl) {
        NSString* text = [event characters];
        if (text.length > 0) {
            const unichar first = [text characterAtIndex:0];
            if (first >= 0x20 && !(first >= 0xF700 && first <= 0xF8FF)) {
                const char* utf8 = text.UTF8String;
                if (utf8) {
                    std::lock_guard<std::mutex> lock(g_inputMutex);
                    g_charQueue += utf8;
                }
            }
        }
    }

    
    return event;
}





NSView* gameView() {
    Class viewClass = NSClassFromString(@"EAGLView");
    if (!viewClass) return nil;

    SEL shared = sel_registerName("sharedEGLView");
    if (![viewClass respondsToSelector:shared]) return nil;

    id view = ((id (*)(id, SEL))objc_msgSend)((id)viewClass, shared);
    if (!view || ![view isKindOfClass:[NSView class]]) return nil;
    return static_cast<NSView*>(view);
}

}

bool macosInputInit() {
    if (!g_view) {
        NSView* view = gameView();
        if (!view) {
            NSLog(@"Grape: macOS input: EAGLView is not ready");
            return false;
        }
        g_view = view;
    }

    
    
    if (!g_keyMonitor) {
        g_keyMonitor = [NSEvent
            addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
                                       handler:^NSEvent*(NSEvent* event) {
                                           return keyMonitor(event);
                                       }];
    }

    if (!g_imguiReady) {
        if (!ImGui_ImplOSX_Init(g_view)) {
            NSLog(@"Grape: macOS input: ImGui_ImplOSX_Init failed");
            return false;
        }
        
        
        
        ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;
        
        
        if (g_view.window) [g_view.window makeFirstResponder:g_view];
        g_imguiReady = true;
    }

    return true;
}

void macosInputShutdown() {
    if (g_keyMonitor) {
        [NSEvent removeMonitor:g_keyMonitor];
        g_keyMonitor = nil;
    }
    g_keyHandler = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_inputMutex);
        g_heldKeys.clear();
        g_keyQueue.clear();
        g_charQueue.clear();
    }
    if (g_imguiReady) {
        ImGui_ImplOSX_Shutdown();
        g_imguiReady = false;
    }
    g_view = nil;
}

void macosInputNewFrame() {
    
    
    
    if (NSView* live = gameView(); live && live != g_view) {
        NSLog(@"Grape: macOS input: rebinding view %p -> %p",
              (__bridge void*)g_view, (__bridge void*)live);
        if (g_imguiReady) ImGui_ImplOSX_Shutdown();
        g_imguiReady = ImGui_ImplOSX_Init(live) != false;
        g_view = live;
        if (g_imguiReady) {
            ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;
            if (live.window) [live.window makeFirstResponder:live];
        }
    }
    
    
    if (g_imguiReady && g_view) ImGui_ImplOSX_NewFrame(g_view);

    
    
    std::vector<KeyEvent> keys;
    std::string characters;
    {
        std::lock_guard<std::mutex> lock(g_inputMutex);
        keys.swap(g_keyQueue);
        characters.swap(g_charQueue);
    }
    for (const KeyEvent& key : keys) {
        if (g_keyHandler) g_keyHandler(key.key, key.down, key.ctrl, key.shift, key.alt);
    }
    
    
    
    if (!characters.empty()) {
        
        const char* app = [[[NSBundle mainBundle] bundlePath] UTF8String];
        if (app) {
            std::ofstream debug(std::string(app) +
                                    "/Contents/geode/Grape/logs/char-debug.log",
                                std::ios::app);
            debug << "chars=" << characters.size()
                  << " accepting=" << (ImGui::GetIO().AppAcceptingEvents ? 1 : 0)
                  << " wantText=" << (ImGui::GetIO().WantTextInput ? 1 : 0)
                  << "\n";
        }
        ImGui::GetIO().AddInputCharactersUTF8(characters.c_str());
    }

    if (!g_keyHandler) return;
    std::vector<int> released;
    {
        std::lock_guard<std::mutex> lock(g_inputMutex);
        for (size_t i = g_heldKeys.size(); i-- > 0;) {
            HeldKey& held = g_heldKeys[i];
            if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                      held.keyCode)) {
                held.upFrames = 0;
                continue;
            }
            if (++held.upFrames < 3) continue;
            
            
            
            const HeldKey releasedKey = held;
            g_heldKeys.erase(g_heldKeys.begin() + static_cast<long>(i));
            const ImGuiKey imguiKey =
                ImGui_ImplOSX_KeyCodeToImGuiKey(releasedKey.keyCode);
            
            
            if (imguiKey != ImGuiKey_None && ImGui::IsKeyDown(imguiKey))
                ImGui::GetIO().AddKeyEvent(imguiKey, false);
            if (releasedKey.key != 0) released.push_back(releasedKey.key);
        }
    }
    if (released.empty()) return;
    const NSEventModifierFlags flags = [NSEvent modifierFlags];
    for (const int key : released)
        g_keyHandler(key, false, (flags & NSEventModifierFlagControl) != 0,
                     (flags & NSEventModifierFlagShift) != 0,
                     (flags & NSEventModifierFlagOption) != 0);
}

void macosInputSetKeyHandler(MacOSKeyHandler handler) {
    g_keyHandler = handler;
}

void macosInputMouseDebug(float* out) {
    for (int i = 0; i < 8; ++i) out[i] = 0.0f;
    if (!g_view || !g_view.window) return;
    const NSPoint screen = [NSEvent mouseLocation];
    const NSPoint inWindow =
        [g_view.window convertRectFromScreen:NSMakeRect(screen.x, screen.y, 0, 0)].origin;
    const NSPoint inView = [g_view convertPoint:inWindow fromView:nil];
    out[0] = (float)screen.x;
    out[1] = (float)screen.y;
    out[2] = (float)inWindow.x;
    out[3] = (float)inWindow.y;
    out[4] = (float)inView.x;
    out[5] = (float)inView.y;
    out[6] = (float)g_view.bounds.size.width;
    out[7] = (float)g_view.bounds.size.height;
}

}
