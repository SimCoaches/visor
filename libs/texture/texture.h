#pragma once

#include <cstddef>
#include <optional>
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <map>

#include <tl/expected.hpp>
#include <glm/vec2.hpp>

namespace sc::texture {

    struct frame {

        glm::ivec2 size;
        std::vector<std::byte> content;
    };

    struct frame_sequence {

        double frame_rate;
        std::vector<frame> frames;

        static std::optional<size_t> plot_frame_index(const double &frame_rate, const size_t &num_frames, double &seconds, const bool &wrap = false);
    };

    struct gpu_handle {

        const uint32_t handle;
        const glm::ivec2 size;
        const std::optional<std::string> description;

        gpu_handle(const uint32_t &handle, const glm::ivec2 &size, const std::optional<std::string> &description);
        gpu_handle(const gpu_handle &) = delete;
        gpu_handle &operator=(const gpu_handle &) = delete;

        ~gpu_handle();
    };

    tl::expected<frame, std::string> load_from_memory(const std::vector<std::byte> &data);
    tl::expected<frame_sequence, std::string> load_lottie_from_memory(const std::string_view &cache_key, const std::vector<std::byte> &data, const glm::ivec2 &size);
    tl::expected<frame, std::string> resize(const frame &reference, const glm::ivec2 &new_size);
    tl::expected<std::shared_ptr<gpu_handle>, std::string> upload_to_gpu(const frame &reference, const glm::ivec2 &size, const std::optional<std::string> &description = std::nullopt);
}