#include "component.hpp"
#include <algorithm>

namespace pi::tui {

void Container::remove(Component* child) {
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) children.erase(it);
}

void Container::clear() {
    children.clear();
}

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

} // namespace pi::tui
