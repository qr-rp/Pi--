#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <algorithm>

namespace pi::tui {

struct InputEvent; // forward from tui.hpp

// ── Component interface ──────────────────────────────────────────────────
struct Component {
    virtual ~Component() = default;
    virtual std::vector<std::string> render(int width) = 0;
    virtual bool handle_input(const InputEvent& ev) { return false; }
    virtual void invalidate() {}
    virtual bool focusable() const { return false; }
    virtual bool focused() const { return false; }
    virtual void focus(bool) {}
};

// ── Container ────────────────────────────────────────────────────────────
class Container : public Component {
public:
    std::vector<Component*> children;

    void add(Component* child) { children.push_back(child); }
    void remove(Component* child);
    void clear();
    std::vector<std::string> render(int width) override;
    void invalidate() override;
    bool handle_input(const InputEvent& ev) override;
};

} // namespace pi::tui
