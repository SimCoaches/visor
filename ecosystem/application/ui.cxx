#include <string_view>
#include <array>
#include <vector>
#include <optional>
#include <functional>
#include <imgui.h>
#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>

#include "application.h"

#include "../font/font_awesome_5.h"
#include "../font/font_awesome_5_brands.h"

glm::dvec2 coords_to_screen(const glm::dvec2 &in, const glm::dvec2 &min, const glm::dvec2 &size) {
    return {
        (in.x * size.x) + min.x,
        (in.y * -size.y) + (min.y + size.y)
    };
}

glm::dvec2 bezier(std::vector<glm::dvec2> inputs, double power, std::optional<std::function<void(const std::vector<glm::dvec2> &level)>> callback = std::nullopt) {
    for (;;) {
        for (int i = 0; i < inputs.size() - 1; i++) inputs[i] = glm::mix(inputs[i], inputs[i + 1], power);
        inputs.resize(inputs.size() - 1);
        if (inputs.size() == 1) return inputs.front();
        if (callback) (*callback)(inputs);
    }
}

template<typename A, typename B, typename C> B xy_as(const A &in) {
    return B {
        static_cast<C>(in.x),
        static_cast<C>(in.y)
    };
}

#define IM_GLMD2(v) xy_as<ImVec2, glm::dvec2, double>(v)
#define GLMD_IM2(v) xy_as<glm::dvec2, ImVec2, float>(v) 

void cubic_bezier_plot(const std::vector<glm::dvec2> &inputs, const glm::ivec2 &size, std::optional<double> fraction = std::nullopt, std::optional<double> limit_min = std::nullopt, std::optional<double> limit_max = std::nullopt) {
    auto draw_list = ImGui::GetWindowDrawList();
    auto bez_area_min = ImGui::GetCursorScreenPos();
    auto bez_area_size = size;
    ImGui::Dummy(GLMD_IM2(size));
    draw_list->AddRectFilled(
        { static_cast<float>(bez_area_min.x), static_cast<float>(bez_area_min.y) },
        { static_cast<float>(bez_area_min.x + bez_area_size.x), static_cast<float>(bez_area_min.y + bez_area_size.y) },
        IM_COL32(255, 255, 255, 32),
        ImGui::GetStyle().FrameRounding
    );
    ImGui::PushClipRect(bez_area_min, { bez_area_min.x + bez_area_size.x, bez_area_min.y + bez_area_size.y }, false);
    bez_area_min.x += 12;
    bez_area_min.y += 12;
    bez_area_size.x -= 24;
    bez_area_size.y -= 24;
    auto screen_p = inputs;
    for (auto &sp : screen_p) sp = coords_to_screen(sp, IM_GLMD2(bez_area_min), bez_area_size);
    for (int i = 1; i < inputs.size(); i++) draw_list->AddLine(GLMD_IM2(screen_p[i - 1]), GLMD_IM2(screen_p[i]), IM_COL32(128, 255, 128, 32), 2.f);
    const int num_curve_segments = 30;
    auto last_plot = GLMD_IM2(coords_to_screen(inputs[0], IM_GLMD2(bez_area_min), bez_area_size));
    for (int i = 1; i < num_curve_segments; i++) {
        const double power = (1.0 / static_cast<double>(num_curve_segments)) * static_cast<double>(i);
        const auto here = GLMD_IM2(coords_to_screen(bezier(inputs, power), IM_GLMD2(bez_area_min), bez_area_size));
        draw_list->AddLine(last_plot, here, IM_COL32(255, 165, 0, 255), 2.f);
        last_plot = here;
    }
    draw_list->AddLine(last_plot, GLMD_IM2(coords_to_screen(inputs.back(), IM_GLMD2(bez_area_min), bez_area_size)), IM_COL32(255, 165, 0, 255), 2.f);
    std::optional<int> hovering_point_i;
    for (int i = 0; i < inputs.size(); i++) {
        auto color = (i == 0 || i == inputs.size() - 1) ? IM_COL32(255, 165, 0, 255) : IM_COL32(255, 255, 255, 128);
        ImGui::SetCursorScreenPos(GLMD_IM2(screen_p[i] - 3.0));
        if (!hovering_point_i && ImGui::IsMouseHoveringRect(GLMD_IM2(screen_p[i] - 3.0), GLMD_IM2(screen_p[i] + 3.0))) {
            ImGui::BeginTooltip();
            ImGui::Text(fmt::format("#{}: x{}, y{}", i + 1, inputs[i].x, inputs[i].y).data());
            ImGui::EndTooltip();
            draw_list->AddRect(GLMD_IM2(screen_p[i] - 6.0), GLMD_IM2(screen_p[i] + 7.0), color, ImGui::GetStyle().FrameRounding, 0, 2);
            hovering_point_i = i;
        } else draw_list->AddRect(GLMD_IM2(screen_p[i] - 3.0), GLMD_IM2(screen_p[i] + 4.0), color, ImGui::GetStyle().FrameRounding, 0, 2);
    }
    {
        const auto top_left = GLMD_IM2(coords_to_screen({ 0, inputs.back().y }, IM_GLMD2(bez_area_min), bez_area_size));
        const auto top_right = GLMD_IM2(coords_to_screen(inputs.back(), IM_GLMD2(bez_area_min), bez_area_size));
        draw_list->AddLine(top_left, top_right, IM_COL32(255, 255, 255, 200), 2.f);
        draw_list->AddText({ top_left.x, top_left.y + 2 }, IM_COL32(255, 255, 255, 128), fmt::format("{}%", static_cast<int>(inputs.back().y * 100.0)).data());
    }
    {
        const auto bottom_right = GLMD_IM2(coords_to_screen({ 1, inputs.front().y }, IM_GLMD2(bez_area_min), bez_area_size));
        const auto bottom_left = GLMD_IM2(coords_to_screen(inputs.front(), IM_GLMD2(bez_area_min), bez_area_size));
        draw_list->AddLine(bottom_left, bottom_right, IM_COL32(255, 255, 255, 200), 2.f);
        const auto text = fmt::format("{}%", static_cast<int>(inputs.front().y * 100.0));
        const auto text_dim = ImGui::CalcTextSize(text.data());
        draw_list->AddText({ bottom_right.x - text_dim.x, bottom_right.y - text_dim.y - 2 }, IM_COL32(255, 255, 255, 128), text.data());
    }
    bez_area_min.x -= 12;
    bez_area_min.y -= 12;
    bez_area_size.x += 24;
    bez_area_size.y += 24;
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos({ bez_area_min.x, bez_area_min.y + bez_area_size.y + ImGui::GetStyle().FramePadding.y });
    /*
    static bool use_smoothing = false;
    static bool use_segments = false;
    ImGui::Checkbox(fmt::format("{}", ICON_FA_SIGNATURE).data(), &use_smoothing);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Use smoothing.");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::Checkbox(fmt::format("{}", ICON_FA_WAVE_SQUARE).data(), &use_segments);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Use segmentation.");
        ImGui::EndTooltip();
    }
    */
}

void emit_axis_profile_slice(std::string_view name) {
    if (ImGui::BeginChild(fmt::format("##{}Window", name).data(), { 0, 0 }, true, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::Text(fmt::format("{} {}", ICON_FA_CUBE, name.data()).data());
            ImGui::EndMenuBar();
        }
        /*
        if (ImGui::BeginChild(fmt::format("##{}DeadzoneWindow", name).data(), { 0, 100 }, true, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                ImGui::Text(fmt::format("{} Deadzone", ICON_FA_PERCENTAGE).data());
                ImGui::EndMenuBar();
            }
            static int dz_top = 0, dz_bottom = 0;
            ImGui::PushItemWidth(120);
            ImGui::SliderInt("Top Deadzone", &dz_top, 0, 15, "%d%%");
            ImGui::SliderInt("Bottom Deadzone", &dz_bottom, 0, 15, "%d%%");
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        */
        if (ImGui::BeginChild(fmt::format("##{}DeadzoneWindow", name).data(), { 0, 100 }, true, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                ImGui::Text(fmt::format("{} Deadzone", ICON_FA_ANCHOR).data());
                ImGui::EndMenuBar();
            }
        }
        ImGui::EndChild();
        if (ImGui::BeginChild(fmt::format("##{}CurveWindow", name).data(), { 340, 0 }, true, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                ImGui::Text(fmt::format("{} Curve", ICON_FA_BEZIER_CURVE).data());
                ImGui::EndMenuBar();
            }
            if (ImGui::BeginCombo(fmt::format("##{}CurveOptions", name).data(), "Linear")) {
                ImGui::Selectable("Option 1");
                ImGui::Selectable("Option 2");
                ImGui::Selectable("Option 3");
                ImGui::Selectable("Option 4");
                ImGui::Selectable("Option 5");
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::Text("Curve Type");
            static std::vector<glm::dvec2> model = {
              { 0.0, 0.0 },
              { 0.2, 0.2 },
              { 0.4, 0.4 },
              { 0.6, 0.6 },
              { 0.8, 0.8 },
              { 1.0, 1.0 }
            };
            const double model_element_min = 0, model_element_max = 1;
            cubic_bezier_plot(model, { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x });
            std::vector<glm::ivec2> model_percents;
            for (auto &e : model) {
                model_percents.push_back({
                    static_cast<int>(e.x * 100.0),
                    static_cast<int>(e.y * 100.0)
                });
            }
            ImGui::PushItemWidth(80);
            for (int i = 0; i < model_percents.size(); i++) {
                if (i == 0 || i == model_percents.size() - 1) continue;
                ImGui::TextDisabled(fmt::format("#{}", i + 1).data());
                ImGui::SameLine();
                if (ImGui::Button(fmt::format("{}##XM{}", ICON_FA_MINUS, i + 1).data())) model_percents[i].x--;
                ImGui::SameLine();
                if (ImGui::Button(fmt::format("{}##XP{}", ICON_FA_PLUS, i + 1).data())) model_percents[i].x++;
                ImGui::SameLine();
                if (ImGui::SliderInt(fmt::format("X##{}", i + 1).data(), &model_percents[i].x, i > 0 ? model_percents[i - 1].x : 0, i == model_percents.size() - 1 ? 100 : model_percents[i + 1].x));
                ImGui::SameLine();
                ImGui::Button(fmt::format("{}##YM{}", ICON_FA_MINUS, i + 1).data());
                ImGui::SameLine();
                ImGui::Button(fmt::format("{}##YP{}", ICON_FA_PLUS, i + 1).data());
                ImGui::SameLine();
                ImGui::SameLine();
                if (ImGui::SliderInt(fmt::format("Y##{}", i + 1).data(), &model_percents[i].y, 0, 100));
                model[i].x = static_cast<double>(glm::clamp(model_percents[i].x, i > 0 ? model_percents[i - 1].x : 0, i == model_percents.size() - 1 ? 100 : model_percents[i + 1].x)) / 100.0;
                model[i].y = static_cast<double>(glm::clamp(model_percents[i].y, 0, 100)) / 100.0;
            }
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild(fmt::format("##{}SettingsWindow", name).data(), { 0, 0 }, true, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                ImGui::Text(fmt::format("{} Settings", ICON_FA_USER_COG).data());
                ImGui::EndMenuBar();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void sc::visor::emit_ui(const glm::ivec2 &framebuffer_size) {
    ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ static_cast<float>(framebuffer_size.x), static_cast<float>(framebuffer_size.y) }, ImGuiCond_Always);
    if (ImGui::Begin("##PrimaryWindow", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu(fmt::format("{} File", ICON_FA_FILE_CODE).data())) {
                ImGui::Selectable(fmt::format("{} Save", ICON_FA_SAVE).data());
                ImGui::Selectable(fmt::format("{} Quit", ICON_FA_SKULL).data());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(fmt::format("{} Services", ICON_FA_CODE).data())) {
                ImGui::Selectable(fmt::format("{} Device Firewall", ICON_FA_TOGGLE_ON).data());
                ImGui::Selectable(fmt::format("{} Virtual Gamepad", ICON_FA_TOGGLE_ON).data());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(fmt::format("{} Devices", ICON_FA_CUBES).data())) {
                if (joysticks.size()) {
                    for (auto &joy_ptr : joysticks) ImGui::Selectable(fmt::format("{} {}", ICON_FA_MICROCHIP, SDL_JoystickName(joy_ptr)).data());
                    ImGui::Separator();
                }                
                ImGui::Selectable(fmt::format("{} Clear System Calibrations", ICON_FA_ERASER).data());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(fmt::format("{} Help", ICON_FA_QUESTION_CIRCLE).data())) {
                ImGui::Selectable(fmt::format("{} Report Bug", ICON_FA_BUG).data());
                ImGui::Selectable(fmt::format("{} Make Comment", ICON_FA_COMMENT_ALT).data());
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if (ImGui::BeginTabBar("##DeviceTabBar")) {
            for (auto &joy_ptr : joysticks) {
                if (ImGui::BeginTabItem(fmt::format("{} {}##{}", ICON_FA_MICROCHIP, SDL_JoystickName(joy_ptr), reinterpret_cast<void *>(&joy_ptr)).data())) {
                    if (ImGui::BeginTabBar("##DeviceSpecificsTabBar")) {
                        if (ImGui::BeginTabItem(fmt::format("{} Hardware", ICON_FA_COG).data())) {
                            if (ImGui::BeginChild("##DeviceMiscInformation", { 0, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                if (ImGui::BeginMenuBar()) {
                                    ImGui::TextDisabled(fmt::format("{} Vendor: {}, Product: {}, Version: {}", ICON_FA_USB, SDL_JoystickGetVendor(joy_ptr), SDL_JoystickGetProduct(joy_ptr), SDL_JoystickGetProductVersion(joy_ptr)).data());
                                    ImGui::EndMenuBar();
                                }
                                if (ImGui::BeginChild("##DeviceHardwareList", { 200, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                    if (ImGui::BeginMenuBar()) {
                                        ImGui::Text(fmt::format("{} Inputs", ICON_FA_SITEMAP).data());
                                        ImGui::EndMenuBar();
                                    }
                                    for (int joy_axis_i = 0; joy_axis_i < SDL_JoystickNumAxes(joy_ptr); joy_axis_i++) {
                                        ImGui::Selectable(fmt::format("{} Axis #{}", ICON_FA_SLIDERS_H, joy_axis_i).data());
                                    }
                                    for (int joy_button_i = 0; joy_button_i < SDL_JoystickNumButtons(joy_ptr); joy_button_i++) {
                                        ImGui::Selectable(fmt::format("{} Button #{}", ICON_FA_TOGGLE_ON, joy_button_i).data());
                                    }
                                }
                                ImGui::EndChild();
                                ImGui::SameLine();
                                if (ImGui::BeginChild("##DeviceHardwareItemCalibration", { 0, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                    if (ImGui::BeginMenuBar()) {
                                        ImGui::Text(fmt::format("{} Calibration", ICON_FA_COGS).data());
                                        ImGui::EndMenuBar();
                                    }
                                }
                                ImGui::EndChild();
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem(fmt::format("{} Profile", ICON_FA_SLIDERS_H).data())) {
                            if (ImGui::BeginChild("##ProfileInformation", { 0, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                if (ImGui::BeginMenuBar()) {
                                    ImGui::TextDisabled(fmt::format("{}", ICON_FA_FOLDER_OPEN).data());
                                    ImGui::SameLine();
                                    ImGui::SetNextItemWidth(180);
                                    if (ImGui::BeginCombo("##ProfileSelector", "Default")) {
                                        ImGui::Selectable("Default");
                                        ImGui::EndCombo();
                                    }
                                    ImGui::SameLine();
                                    ImGui::Button(fmt::format("{}##ButtonProfileDelete", ICON_FA_MINUS_SQUARE).data());
                                    ImGui::SameLine();
                                    ImGui::Button(fmt::format("{}##ButtonProfileAdd", ICON_FA_PLUS_SQUARE).data());
                                    ImGui::EndMenuBar();
                                }
                                if (ImGui::BeginChild("##ProfileInputList", { 200, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                    if (ImGui::BeginMenuBar()) {
                                        ImGui::Text(fmt::format("{} Inputs", ICON_FA_SITEMAP).data());
                                        ImGui::EndMenuBar();
                                    }
                                    for (int joy_axis_i = 0; joy_axis_i < SDL_JoystickNumAxes(joy_ptr); joy_axis_i++) {
                                        ImGui::Selectable(fmt::format("{} Axis #{}", ICON_FA_SLIDERS_H, joy_axis_i).data());
                                    }
                                    for (int joy_button_i = 0; joy_button_i < SDL_JoystickNumButtons(joy_ptr); joy_button_i++) {
                                        ImGui::Selectable(fmt::format("{} Button #{}", ICON_FA_TOGGLE_ON, joy_button_i).data());
                                    }
                                }
                                ImGui::EndChild();
                                ImGui::SameLine();
                                emit_axis_profile_slice("Clutch");
                                /*
                                ImGui::SameLine();
                                emit_axis_profile_slice("Brake");
                                ImGui::SameLine();
                                emit_axis_profile_slice("Throttle");
                                ImGui::EndTabItem();
                                */
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}