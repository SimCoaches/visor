add_library(boot STATIC
    "gl-proc-address.cxx"
    "gl-dbg-msg-cb.cxx"
)

target_link_libraries(boot
    CONAN_PKG::spdlog
    CONAN_PKG::fmt
    CONAN_PKG::glfw
    CONAN_PKG::glbinding
    
    sentry
)