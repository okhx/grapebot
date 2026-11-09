#pragma once

#include <memory>
#include <string>
#include <vector>

namespace grape::scripting {
struct ScriptStatus {
    std::string name;
    std::string status;
    bool loaded;
};

class ScriptEngine {
public:
    static ScriptEngine& get();
    ~ScriptEngine();

    void refresh();
    std::vector<ScriptStatus> scripts() const;
    bool load(const std::string& name);
    void unload(const std::string& name);
    void update(double dt);
    void input(int player, int button, bool pressed);
    void overlay();
    void draw();

private:
    ScriptEngine();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
