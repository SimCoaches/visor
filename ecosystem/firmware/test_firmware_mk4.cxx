#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

#include "../hidapi/hidapi.h"

#include "mk4.h"

int main() {
    spdlog::default_logger()->set_level(spdlog::level::debug);
    const auto res = sc::firmware::mk4::discover();
    if (!res.has_value()) {
        spdlog::error(res.error());
        return 1;
    }
    for (auto device : *res) {
        const auto res = device->get_version();
        if (!res.has_value()) {
            spdlog::error(res.error());
            return 2;
        }
        spdlog::info("Firmware version: MK {}.{}.{}", std::get<0>(*res), std::get<1>(*res), std::get<2>(*res));
        /*
        for (;;) {
            const auto tell = device->read(4000);
            if (!tell.has_value()) {
                spdlog::error(tell.error());
                continue;
            }
            if (!tell.value().has_value()) {
                spdlog::warn("No data received.");
                continue;
            }
            spdlog::info(reinterpret_cast<const char *>(tell.value().value().data()));
        }
        */
    }
    return 0;
}