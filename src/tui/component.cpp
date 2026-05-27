#include "component.hpp"
#include "tui.hpp"

namespace pi::tui {

void Container::remove(Component* child) {
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) children.erase(it);
}

void Container::clear() { children.clear(); }

std::vector<std::string> Container::render(int width) {
    std::vector<std::string> result;
    for (auto* child : children) {
        auto lines = child->render(width);
        result.insert(result.end(), lines.begin(), lines.end());
    }
    return result;
}

void Container::invalidate() {
    Component::invalidate();
    for (auto* child : children) child->invalidate();
}

bool Container::handle_input(const InputEvent& ev) {
    for (auto* child : children) {
        if (child->focused() && child->handle_input(ev))
            return true;
    }
    // Fallback: try each child in reverse (last focused first)
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if ((*it)->handle_input(ev))
            return true;
    }
    return false;
}

} // namespace pi::tui
