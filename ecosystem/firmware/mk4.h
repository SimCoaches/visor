#pragma once

#include <tl/expected.hpp>

#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <memory>

namespace sc::firmware::mk4 {

    struct device_handle {

        struct axis_info {

            bool enabled;
            int8_t curve_i;
            uint16_t min, max;
        };

        const uint16_t vendor, product;
        const std::string org, name, uuid, serial;
        void * const ptr;

        uint16_t _communications_id = 0;
        uint16_t _next_packet_id = 0;

        device_handle(const uint16_t &vendor, const uint16_t &product, const std::string_view &org, const std::string_view &name, const std::string_view &uuid, const std::string_view &serial, void * const ptr);
        device_handle(const device_handle&) = delete;
        device_handle &operator=(const device_handle &) = delete;
        ~device_handle();

        std::optional<std::string> write(const std::array<std::byte, 64> &packet);
        tl::expected<std::optional<std::array<std::byte, 64>>, std::string> read(const std::optional<int> &timeout = std::nullopt);
        tl::expected<uint16_t, std::string> get_new_communications_id();
        tl::expected<std::tuple<uint16_t, uint16_t, uint16_t>, std::string> get_version();
        tl::expected<uint8_t, std::string> get_num_axes();
        tl::expected<axis_info, std::string> get_axis_state(const int &index);
    };

    tl::expected<std::vector<std::shared_ptr<device_handle>>, std::string> discover(const std::optional<std::vector<std::shared_ptr<device_handle>>> &existing = std::nullopt);
}