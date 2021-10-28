#include "legacy.h"

#include <optional>

#include <spdlog/spdlog.h>

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Xinput.h>

#include <GLFW/glfw3.h>

#include "../../libs/hidhide/hidhide.h"
#include "../../libs/vigem/client.h"

#include <glm/common.hpp>

#include "bezier.h"

#undef min
#undef max

std::array<sc::visor::legacy::axis_info, 4> sc::visor::legacy::axes;
std::array<sc::visor::legacy::model, 5> sc::visor::legacy::models;

std::optional<int> sc::visor::legacy::axis_i_throttle;
std::optional<int> sc::visor::legacy::axis_i_brake;
std::optional<int> sc::visor::legacy::axis_i_clutch;

namespace sc::visor::legacy {

    static std::optional<PVIGEM_CLIENT> vigem_client;
    static std::optional<PVIGEM_TARGET> vigem_gamepad;
    static std::optional<DS4_REPORT> vigem_gamepad_report;

    static std::optional<std::filesystem::path> get_module_file_path() {
        TCHAR path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, sizeof(path)) == 0) return std::nullopt;
        return path;
    }

    static std::optional<std::string> whitelist_this_module() {
        if (auto whitelist = hidhide::get_whitelist(); whitelist.has_value()) {
            if (auto module_path = get_module_file_path(); module_path.has_value()) {
                if (auto image_path = hidhide::convert_path_to_image_path(module_path.value()); image_path.has_value()) {
                    bool whitelisted = false;
                    for (auto &path : *whitelist) {
                        spdlog::debug("HIDHIDE whitelist entry: {}", path.string());
                        if (path == image_path.value()) whitelisted = true;
                    }
                    if (whitelisted) return std::nullopt;
                    else {
                        whitelist->push_back(*image_path);
                        if (hidhide::set_whitelist(*whitelist)) {
                            spdlog::debug("Added this module to HIDHIDE whitelist: {}", image_path->string());
                            return std::nullopt;
                        } else return fmt::format("Unable to add this module to HIDHIDE whitelist: {}", image_path->string());
                    }
                } else return "Unable to convert module path to image path.";
            } else return "Unable to get path for this module.";
        } else return "Unable to get HIDHIDE whitelist";
    }

    static std::optional<std::string> sync_blacklist() {
        if (auto whitelist = hidhide::get_whitelist(); whitelist.has_value()) {
            if (whitelist->size() > 0) {
                for (auto &item : *whitelist) {
                    spdlog::debug("HIDHIDE whitelist entry: {}", item.string());
                }
            } else spdlog::debug("HIDHIDE whitelist has no entries.");
        } else return "Unable to get HIDHIDE whitelist.";
        if (auto blacklist = hidhide::get_blacklist(); blacklist.has_value()) {
            for (auto &item : *blacklist) spdlog::debug("HID blacklist entry: {}", item);
            if (auto devices = hidhide::list_devices(); devices.has_value()) {
                std::vector<std::string> to_hide;
                for (auto &device : *devices) {
                    if (device.product_name != "Sim Coaches P1 Pro Pedals") continue;
                    if (std::find(blacklist->begin(), blacklist->end(), device.instance_path) != blacklist->end()) {
                        spdlog::debug("HIDHIDE entry \"{}\" already exists.", device.instance_path);
                        continue;
                    }
                    spdlog::info("Hiding device: {} -> {}", device.instance_path, device.product_name);
                    to_hide.push_back(device.instance_path);
                }
                if (to_hide.size()) {
                    if (hidhide::set_blacklist(to_hide)) spdlog::info("Updated HID blacklist.");
                    else return "Unable to update HIDHIDE blacklist.";
                } else spdlog::debug("No HIDHIDE blacklist update is needed.");
            }
        } else return "Unable to get HIDHIDE blacklist.";
        return std::nullopt;
    }
}

std::optional<std::string> sc::visor::legacy::enable() {
    if (!hidhide::present()) return "Unable to detect HIDHIDE driver.";
    if (!hidhide::is_enabled() && !hidhide::set_enabled(true)) return "Unable to activate HIDHIDE.";
    if (const auto err = sync_blacklist(); err) return err;
    if (const auto err = whitelist_this_module(); err) return err;
    if (auto new_vigem_client = vigem_alloc(); new_vigem_client) {
        if (VIGEM_SUCCESS(vigem_connect(new_vigem_client))) {
            if (auto new_vigem_gamepad = vigem_target_ds4_alloc(); new_vigem_gamepad) {
                vigem_target_set_pid(new_vigem_gamepad, 0x1209);
                vigem_target_set_vid(new_vigem_gamepad, 0x0070);
                if (VIGEM_SUCCESS(vigem_target_add(new_vigem_client, new_vigem_gamepad))) {
                    vigem_client = new_vigem_client;
                    vigem_gamepad = new_vigem_gamepad;
                    spdlog::debug("Legacy support enabled.");
                    return std::nullopt;
                } else {
                    vigem_target_free(new_vigem_gamepad);
                    vigem_disconnect(new_vigem_client);
                    vigem_free(new_vigem_client);
                    return "Unable to activate ViGEmBus gamepad.";
                }
            } else {
                vigem_disconnect(new_vigem_client);
                vigem_free(new_vigem_client);
                return "Unable to allocate required memory for ViGEmBus gamepad target.";
            }
        } else {
            vigem_free(new_vigem_client);
            return "Unable to connect to ViGEmBus driver.";
        }
    } else return "Unable to allocate required memory for ViGEmBus driver connection.";
}

void sc::visor::legacy::disable() {
    if (vigem_gamepad) {
        if (vigem_client) vigem_target_remove(*vigem_client, *vigem_gamepad);
        vigem_target_free(*vigem_gamepad);
        vigem_gamepad.reset();
        spdlog::debug("Disconnected gamepad from ViGEmBus driver.");
    }
    if (vigem_client) {
        vigem_disconnect(*vigem_client);
        vigem_free(*vigem_client);
        vigem_client.reset();
        spdlog::debug("Disconnected from ViGEmBus driver.");
    }
    vigem_gamepad_report.reset();
    spdlog::debug("Legacy support disabled.");
}

std::optional<std::string> sc::visor::legacy::process() {
    for (auto &axis : axes) {
        axis.present = false;
        axis.input_raw = 0.f;
    }
    if (!vigem_client || !vigem_gamepad) return std::nullopt;
    if (!vigem_gamepad_report) {
        vigem_gamepad_report = DS4_REPORT();
        DS4_REPORT_INIT(&*vigem_gamepad_report);
        spdlog::debug("Initialized gamepad USB report structure.");
    }
    bool found_legacy_hardware = false;
    for (int i = 0; i < GLFW_JOYSTICK_LAST; i++) {
        if (!glfwJoystickPresent(i)) continue;
        if (strcmp("Sim Coaches P1 Pro Pedals", glfwGetJoystickName(i)) != 0) continue;
        int num_axes;
        const auto inputs = glfwGetJoystickAxes(i, &num_axes);
        if (!inputs) continue;
        for (int j = 0; j < glm::min(num_axes, static_cast<int>(axes.size())); j++) {
            axes[j].present = true;
            float value = glm::max(0.f, glm::min(1.f, (inputs[j] + 1.f) * .5f));
            axes[j].input_raw = value;
            axes[j].input_steps = glm::round(value * 1000.f);
            auto max_input = axes[j].output_steps_max / 1000.f;
            auto min_input = (axes[j].output_steps_min + ((axes[j].output_steps_max - axes[j].output_steps_min) * (axes[j].deadzone / 100.f))) / 1000.f;
            if (min_input > max_input) min_input = max_input;
            value -= min_input;
            value *= 1.f / (max_input - min_input);
            value = glm::min(1.f, glm::max(0.f, value));
            value = glm::min(value, axes[j].output_limit / 100.f);
            if (j == 0) vigem_gamepad_report->bThumbLX = glm::round(255 * value);
            else if (j == 1) vigem_gamepad_report->bThumbLY = glm::round(255 * value);
            else if (j == 2) vigem_gamepad_report->bThumbRX = glm::round(255 * value);
            else if (j == 3) vigem_gamepad_report->bThumbRY = glm::round(255 * value);
            axes[j].output = value;
        }
        found_legacy_hardware = true;
        break;
    }
    if (!found_legacy_hardware && vigem_gamepad_report) DS4_REPORT_INIT(&*vigem_gamepad_report);
    if (!VIGEM_SUCCESS(vigem_target_ds4_update(*vigem_client, *vigem_gamepad, *vigem_gamepad_report))) {
        disable();
        return "Unable to update ViGEmBus gamepad.";
    }
    return std::nullopt;
}

bool sc::visor::legacy::present() {
    return axes.size() > 0;
}