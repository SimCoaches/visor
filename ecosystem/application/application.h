#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <SDL2/SDL_joystick.h>

namespace sc::visor {

    extern std::vector<SDL_Joystick *> joysticks;

    void emit_ui(const glm::ivec2 &framebuffer_size);
}