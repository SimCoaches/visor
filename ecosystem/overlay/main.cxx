#include "version.h"

#define SC_FEATURE_SENTRY
#define SC_FEATURE_MINIMAL_REDRAW
#define SC_FEATURE_ENHANCED_FONTS
#define SC_FEATURE_RENDER_ON_RESIZE
#define SC_FEATURE_CENTER_WINDOW
#define SC_FEATURE_TRANSPARENT_WINDOW

#define SC_VIEW_INIT_W 400
#define SC_VIEW_INIT_H 600
#define SC_VIEW_MIN_W SC_VIEW_INIT_W
#define SC_VIEW_MIN_H SC_VIEW_INIT_H

#include "../boot/imgui_gl3_glfw3.hpp"

static std::optional<std::string> sc::boot::on_startup() {
    return std::nullopt;
}

static tl::expected<bool, std::string> sc::boot::on_fixed_update() {
    return true;
}

static tl::expected<bool, std::string> sc::boot::on_update(const glm::ivec2 &framebuffer_size, bool *const force_redraw) {
    return true;
}

static void sc::boot::on_shutdown() {

}