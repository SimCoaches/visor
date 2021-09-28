#include "gui.h"

#include <string_view>
#include <array>
#include <vector>
#include <optional>
#include <functional>
#include <future>
#include <imgui.h>
#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <spdlog/spdlog.h>
#include <pystring.h>

#include "application.h"

#include "../font/font_awesome_5.h"
#include "../font/font_awesome_5_brands.h"
#include "../imgui/imgui_utils.hpp"
#include "../defer.hpp"
#include "../resource/resource.h"
#include "../texture/texture.h"
#include "../file/file.h"
#include "../firmware/mk4.h"

#include <stb_image_write.h>

namespace sc::visor::gui {

    static bool animation_play = false;
    static bool animation_playing = false;
    static bool animation_loop = false;
    static double animation_time = 0;
    static double animation_frame_rate = 0;
    static size_t animation_frame_i = 0;
    static std::vector<std::shared_ptr<sc::texture::gpu_handle>> animation_frames;

    static std::optional<std::chrono::high_resolution_clock::time_point> devices_last_scan;
    static std::future<tl::expected<std::vector<std::shared_ptr<sc::firmware::mk4::device_handle>>, std::string>> devices_future;
    static std::vector<std::shared_ptr<firmware::mk4::device_handle>> devices;

    struct device_context {

        std::optional<std::chrono::high_resolution_clock::time_point> last_communication;
        std::shared_ptr<firmware::mk4::device_handle> handle;
        std::string name, serial;
    };

    static std::vector<std::shared_ptr<device_context>> device_contexts;

    static std::optional<std::string> prepare_styling() {
        auto &style = ImGui::GetStyle();
        style.WindowBorderSize = 1;
        style.FrameBorderSize = 1;
        style.FrameRounding = 2.f;
        style.ChildRounding = 2.f;
        style.ScrollbarRounding = 2.f;
        style.WindowRounding = 2.f;
        style.GrabRounding = 2.f;
        style.TabRounding = 2.f;
        style.Colors[ImGuiCol_ChildBg] = { .09f, .09f, .09f, 1.f };
        return std::nullopt;
    }

    void load_animations() {
        const auto resource_name = "LOTTIE_CUBES";
        if (const auto content = sc::resource::get_resource("DATA", resource_name); content) {
            std::vector<std::byte> buffer(content->second);
            memcpy(buffer.data(), content->first, buffer.size());
            if (const auto sequence = sc::texture::load_lottie_from_memory(resource_name, buffer, { 200, 200 }); sequence.has_value()) {
                animation_frame_rate = sequence->frame_rate;
                int frame_i = 0;
                for (const auto &frame : sequence->frames) {
                    const auto description = pystring::lower(fmt::format("<rsc:{}:{}x{}#{}>", resource_name, sequence->frames.front().size.x, sequence->frames.front().size.y, frame_i++));
                    if (const auto texture = sc::texture::upload_to_gpu(frame, sequence->frames.front().size, description); texture.has_value()) {
                        animation_frames.push_back(*texture);
                    } else spdlog::error(texture.error());
                }
                animation_frames.resize(60);
            }
        }
    }

    static void scan_for_devices() {
        animation_play = true;
        animation_loop = true;
        devices_future = async(std::launch::async, []() {
            return sc::firmware::mk4::discover(devices);
        });
        spdlog::debug("Device scan started.");
    }

    static void poll_devices() {
        if (devices_future.valid()) {
            if (devices_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    auto res = devices_future.get();
                    DEFER({
                        devices_future = { };
                        animation_loop = false;
                    });
                    if (!res.has_value()) {
                        spdlog::error(res.error());
                        return;
                    }
                    for (auto &new_device : *res) devices.push_back(new_device);
                    devices_last_scan = std::chrono::high_resolution_clock::now();
                    spdlog::debug("Found {} devices.", res->size());
                } catch(const std::exception& ex) {
                    spdlog::critical("Exception");
                }
            }
        } else {
            if (!devices_last_scan) devices_last_scan = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - *devices_last_scan).count() >= 1) scan_for_devices();
        }
        for (auto &device : devices) {
            const auto contexts_i = std::find_if(device_contexts.begin(), device_contexts.end(), [&device](const std::shared_ptr<device_context> &context) {
                return context->serial == device->serial;
            });
            if (contexts_i != device_contexts.end()) {
                if (contexts_i->get()->handle.get() != device.get()) {
                    spdlog::debug("Applying new handle to existing device context. (Serial: {})", device->serial);
                    contexts_i->get()->handle = device;
                }
                continue;
            }
            spdlog::debug("Creating new device context. (Serial: {})", device->serial);
            auto new_device_context = std::make_shared<device_context>();
            new_device_context->handle = device;
            new_device_context->name = device->name;
            new_device_context->serial = device->serial;
            device_contexts.push_back(new_device_context);
        }
        for (auto &context : device_contexts) {
            if (!context->handle) continue;
            const auto now = std::chrono::high_resolution_clock::now();
            if (!context->last_communication) context->last_communication = now;
            if (std::chrono::duration_cast<std::chrono::seconds>(now - *context->last_communication).count() >= 2) {
                context->last_communication = now;
                const auto res = context->handle->version();
                if (!res) {
                    spdlog::error("Device context error: {}", res.error());
                    devices.erase(std::remove_if(devices.begin(), devices.end(), [&](const std::shared_ptr<firmware::mk4::device_handle> &device) {
                        return device.get() == context->handle.get();
                    }), devices.end());
                    context->handle.reset();
                }
            }
        }
    }

    enum class view {
        device_panel,
        notify_saved_panel,
    };

    static view current_view = view::device_panel;

    template<typename A, typename B, typename C> B xy_as(const A &in) {
        return B {
            static_cast<C>(in.x),
            static_cast<C>(in.y)
        };
    }

    #define IM_GLMD2(v) xy_as<ImVec2, glm::dvec2, double>(v)
    #define GLMD_IM2(v) xy_as<glm::dvec2, ImVec2, float>(v) 

    static glm::dvec2 coords_to_screen(const glm::dvec2 &in, const glm::dvec2 &min, const glm::dvec2 &size) {
        return {
            (in.x * size.x) + min.x,
            (in.y * -size.y) + (min.y + size.y)
        };
    }

    static glm::dvec2 bezier(std::vector<glm::dvec2> inputs, double power, std::optional<std::function<void(const std::vector<glm::dvec2> &level)>> callback = std::nullopt) {
        for (;;) {
            for (int i = 0; i < inputs.size() - 1; i++) inputs[i] = glm::mix(inputs[i], inputs[i + 1], power);
            inputs.resize(inputs.size() - 1);
            if (inputs.size() == 1) return inputs.front();
            if (callback) (*callback)(inputs);
        }
    }

    static void cubic_bezier_plot(const std::vector<glm::dvec2> &inputs, const glm::ivec2 &size, std::optional<double> fraction = std::nullopt, std::optional<double> limit_min = std::nullopt, std::optional<double> limit_max = std::nullopt) {
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
    }

    static void emit_content_notify_saved_panel() {
        ImPenUtility pen;
        pen.CalculateWindowBounds();
        ImGui::SetCursorScreenPos(pen.GetCenteredPosition({ 300, 150 }));
        if (ImGui::BeginChild("##NotifySavedWindow", { 300, 150 }, true, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                ImGui::Text(fmt::format("Saved", ICON_FA_SITEMAP).data());
                ImGui::EndMenuBar();
            }
            ImGui::Text("All data has been saved.");
            if (ImGui::Button("Okay")) current_view = view::device_panel;
        }
        ImGui::EndChild();
    }

    static void emit_content_device_panel() {
        enum class selection_type {
            axis,
            button,
            hat
        };
        if (device_contexts.size()) {
            animation_time = 0;
            if (ImGui::BeginTabBar("##DeviceTabBar")) {
                for (const auto &context : device_contexts) {
                    if (ImGui::BeginTabItem(fmt::format("{} {}##{}", ICON_FA_MICROCHIP, context->name, context->serial).data())) {
                        if (context->handle) ImGui::TextColored({ .2f, 1, .2f, 1 }, fmt::format("{} Connected.", ICON_FA_CHECK_DOUBLE).data());
                        else ImGui::TextColored({ 1, 1, .2f, 1 }, fmt::format("{} Not connected.", ICON_FA_SPINNER).data());
                        if (ImGui::BeginTabBar("##DeviceSpecificsTabBar")) {
                            if (ImGui::BeginTabItem(fmt::format("{} Hardware", ICON_FA_COG).data())) {
                                static std::optional<std::pair<selection_type, int>> current_selection;
                                if (ImGui::BeginChild("##DeviceMiscInformation", { 0, 0 }, true)) {
                                    if (ImGui::BeginChild("##DeviceHardwareList", { 200, 0 }, true, ImGuiWindowFlags_MenuBar)) {
                                        if (ImGui::BeginMenuBar()) {
                                            ImGui::Text(fmt::format("{} Inputs", ICON_FA_SITEMAP).data());
                                            ImGui::EndMenuBar();
                                        }
                                    }
                                    ImGui::EndChild();
                                    ImGui::SameLine();
                                }
                                ImGui::EndChild();
                                ImGui::EndTabItem();
                            }
                            if (ImGui::BeginTabItem(fmt::format("{} Profile", ICON_FA_SLIDERS_H).data())) {
                                static std::optional<std::pair<selection_type, int>> current_selection;
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
                                    }
                                    ImGui::EndChild();
                                    ImGui::SameLine();
                                    if (current_selection) {
                                        switch (current_selection->first) {
                                            case selection_type::axis:
                                                // emit_axis_profile_slice(joy, current_selection->second);
                                                break;
                                            case selection_type::button:
                                                break;
                                            case selection_type::hat:
                                                break;
                                        }
                                    }
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
        } else {
            ImPenUtility pen;
            pen.CalculateWindowBounds();
            const auto image_pos = pen.GetCenteredPosition(GLMD_IM2(animation_frames[animation_frame_i]->size));
            ImGui::SetCursorScreenPos(image_pos);
            if (animation_frames.size()) {
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(animation_frames[animation_frame_i]->handle),
                    GLMD_IM2(animation_frames[animation_frame_i]->size),
                    { 0, 0 },
                    { 1, 1 },
                    { 1, 1, 1, animation_playing ? 1 : 0.8f }
                );
            }
        }
    }

    static void emit_content_panel() {
        switch (current_view) {
            case view::device_panel:
                emit_content_device_panel();
                break;
            case view::notify_saved_panel:
                emit_content_notify_saved_panel();
                break;
        }
    }

    static void emit_primary_window_menu_bar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu(fmt::format("{} File", ICON_FA_FILE_CODE).data())) {
                // if (ImGui::Selectable(fmt::format("{} Theme", ICON_FA_PAINT_ROLLER).data()));
                if (ImGui::Selectable(fmt::format("{} Quit", ICON_FA_SKULL).data())) keep_running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(fmt::format("{} Devices", ICON_FA_CUBES).data())) {
                if (devices.size()) {
                    for (auto &device : devices) ImGui::TextDisabled(fmt::format("{} {} {} (#{})", ICON_FA_MICROCHIP, device->org, device->name, device->serial).data());
                    ImGui::Selectable(fmt::format("{} Clear System Calibrations", ICON_FA_ERASER).data());
                    if (ImGui::Selectable(fmt::format("{} Release All", ICON_FA_STOP).data())) {
                        devices.clear();
                        device_contexts.clear();
                    }
                } else ImGui::TextDisabled("No devices.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(fmt::format("{} Help", ICON_FA_QUESTION_CIRCLE).data())) {
                ImGui::Selectable(fmt::format("{} Report Bug", ICON_FA_BUG).data());
                ImGui::Selectable(fmt::format("{} Make Comment", ICON_FA_COMMENT_ALT).data());
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
}

void sc::visor::gui::initialize() {
    prepare_styling();
    load_animations();
}

void sc::visor::gui::shutdown() {
    devices_future = { };
    devices.clear();
    animation_frames.clear();
}

void sc::visor::gui::emit(const glm::ivec2 &framebuffer_size, bool *const force_redraw) {
    static auto last = std::chrono::high_resolution_clock::now();
    const auto now = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
    last = now;
    if (animation_play && !animation_playing) {
        animation_time = 0;
        animation_playing = true;
        animation_play = false;
    }
    if (animation_playing) {
        const double delta = static_cast<double>(elapsed.count()) / 1000.0;
        animation_time += delta;
        if (const auto res = texture::frame_sequence::plot_frame_index(animation_frame_rate, animation_frames.size(), animation_time, animation_loop); res && animation_frame_i != *res) {
            animation_frame_i = *res;
            if (force_redraw) *force_redraw = true;
        }
        if (!animation_loop && animation_frame_i == animation_frames.size() - 1) animation_playing = false;
    }
    ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ static_cast<float>(framebuffer_size.x), static_cast<float>(framebuffer_size.y) }, ImGuiCond_Always);
    if (ImGui::Begin("##PrimaryWindow", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
        emit_primary_window_menu_bar();
        emit_content_panel();
    }
    ImGui::End();
    poll_devices();
}