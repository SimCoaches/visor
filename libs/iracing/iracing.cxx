#include "iracing.h"

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <chrono>
#include <algorithm>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

#include <windows.h>

#include "../defer.hpp"

namespace sc::iracing {

    static const auto mapped_file_name = "Local\\IRSDKMemMapFileName";
    static const size_t mapped_file_length = 1164 * 1024;
    static const auto data_event_name = "Local\\IRSDKDataValidEvent";
    static const auto broadcast_message_name = "IRSDK_BROADCASTMSG";

    static std::thread worker;
    static std::atomic_bool working = false;

    static std::atomic<status> current_status = status::stopped;

    struct variable_buffer_header {

        int32_t tick_count;
        int32_t data_offset;
        uint32_t _padding_1[2];
    };

    struct header {

        int32_t version;
        int32_t status;
        int32_t tick_rate;
        int32_t session_info_update;
        int32_t session_info_length;
        int32_t session_info_offset;
        int32_t num_variables;
        int32_t variables_header_offset;
        int32_t num_buffers;
        int32_t buffer_length;
        uint32_t _padding_1[2];
        variable_buffer_header buffers[4];
    };

    struct variable_header {

        int32_t type;
        int32_t offset;
        int32_t count;
        std::byte count_as_time;
        uint8_t _padding_1[3];
        std::byte name[32];
        std::byte description[64];
        std::byte unit[32];
    };

    static variable_buffer_header *find_recent_valid_buffer(header *telemetry_header) {
        std::vector<variable_buffer_header *> sorted_buffers;
        for (int i = 0; i < telemetry_header->num_buffers; i++) sorted_buffers.push_back(&telemetry_header->buffers[i]);
        std::sort(sorted_buffers.begin(), sorted_buffers.end(), [](variable_buffer_header *first, variable_buffer_header *second) {
            return first->tick_count > second->tick_count;
        });
        return sorted_buffers[1];
    }

    static void process_telemetry(header *telemetry_header, variable_header *variables) {
        auto variable_buffer = find_recent_valid_buffer(telemetry_header);
        for (int i = 0; i < telemetry_header->num_variables; i++) {
            if (strcmp("RPM", reinterpret_cast<char *>(variables[i].name)) == 0) {
                auto value = reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(telemetry_header) + variable_buffer->data_offset + variables[i].offset);
                spdlog::info("RPM: {}, {}", *value, variables[i].type);
            } else if (strcmp("LapDistPct", reinterpret_cast<char *>(variables[i].name)) == 0) {
                auto value = reinterpret_cast<float *>(reinterpret_cast<uintptr_t>(telemetry_header) + variable_buffer->data_offset + variables[i].offset);
                spdlog::info("LapDistPct: {}, {}", *value, variables[i].type);
            } else if (strcmp("IsOnTrack", reinterpret_cast<char *>(variables[i].name)) == 0) {
                auto value = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(telemetry_header) + variable_buffer->data_offset + variables[i].offset);
                spdlog::info("IsOnTrack: {}, {}", static_cast<int>(*value), variables[i].type);
            }
        }
    }

    static void work() {
        DEFER(
            current_status = status::stopped;
        );
        std::optional<std::chrono::system_clock::time_point> last_file_handle_open_attempt;
        for (;;) {
            current_status = status::searching;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!working) return;
            if (!last_file_handle_open_attempt || std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - *last_file_handle_open_attempt).count() > 1) {
                std::optional<HANDLE> file_handle;;
                std::optional<std::byte *> mapped_file_buffer;
                std::optional<HANDLE> event_handle;
                DEFER(
                    if (event_handle) {
                        CloseHandle(*event_handle);
                        spdlog::debug("Closed iRacing event handle.");
                        event_handle.reset();
                    }
                    if (mapped_file_buffer) {
                        UnmapViewOfFile(*mapped_file_buffer);
                        spdlog::debug("Unmapped iRacing memory file.");
                        mapped_file_buffer.reset();
                    }
                    if (file_handle) {
                        CloseHandle(*file_handle);
                        spdlog::debug("Closed iRacing memory file handle.");
                        file_handle.reset();
                    }
                    last_file_handle_open_attempt = std::chrono::system_clock::now();
                );
                if (const auto attempted_handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mapped_file_name); attempted_handle) {
                    file_handle = attempted_handle;
                    if (const auto attempted_mapped_buffer = MapViewOfFile(*file_handle, FILE_MAP_ALL_ACCESS, 0, 0, mapped_file_length); attempted_mapped_buffer) {
                        mapped_file_buffer = reinterpret_cast<std::byte *>(attempted_mapped_buffer);
                        if (auto attempted_event_handle = OpenEventA(SYNCHRONIZE, FALSE, data_event_name); attempted_event_handle) {
                            event_handle = attempted_event_handle;
                            spdlog::debug("iRacing telemetry is online.");
                            while (working) {
                                const auto wait_res = WaitForSingleObject(*event_handle, 1000);
                                if (wait_res == WAIT_OBJECT_0) {
                                    current_status = status::live;
                                    process_telemetry(
                                        reinterpret_cast<header *>(*mapped_file_buffer),
                                        reinterpret_cast<variable_header *>(reinterpret_cast<uintptr_t>(*mapped_file_buffer) + reinterpret_cast<header *>(*mapped_file_buffer)->variables_header_offset)
                                    );
                                } else if (wait_res == WAIT_TIMEOUT) {
                                    current_status = status::connected;
                                } else if (wait_res == WAIT_ABANDONED) {
                                    spdlog::warn("iRacing synchronization handle was abandoned.");
                                    break;
                                }
                                else if (wait_res == WAIT_FAILED) {
                                    spdlog::warn("iRacing synchronization handle returned fail state.");
                                    break;
                                }
                            }
                        } else spdlog::warn("Unable to open iRacing synchronization handle.");
                    } else spdlog::warn("Unable to map iRacing memory file.");
                }
            }
        }
    }
}

void sc::iracing::startup() {
    shutdown();
    spdlog::debug("Starting up iRacing telemetry worker.");
    working = true;
    worker = std::thread(work);
}

void sc::iracing::shutdown() {
    working = false;
    if (worker.joinable()) {
        worker.join();
        spdlog::debug("Shutdown iRacing telemetry worker.");
    }
}

sc::iracing::status sc::iracing::get_status() {
    return current_status;
}