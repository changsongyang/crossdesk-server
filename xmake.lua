set_project("crossdesk_server")
set_version("0.0.1")

add_rules("mode.release", "mode.debug")
set_languages("c++17")

add_rules("mode.release", "mode.debug")

add_requires("asio 1.24.0", "nlohmann_json 3.11.3", "spdlog 1.14.1", "sqlite3 3.49.0", "openssl 1.1.1-w", "websocketpp 0.8.2")

add_defines("ASIO_STANDALONE", "ASIO_HAS_STD_TYPE_TRAITS",
    "ASIO_HAS_STD_SHARED_PTR", "ASIO_HAS_STD_ADDRESSOF", "ASIO_HAS_STD_ATOMIC",
    "ASIO_HAS_STD_CHRONO", "ASIO_HAS_CSTDINT", "ASIO_HAS_STD_ARRAY",
    "ASIO_HAS_STD_SYSTEM_ERROR", "SIGNAL_LOGGER")

if is_os("windows") then
    add_defines("_WEBSOCKETPP_CPP11_INTERNAL_")
    add_links("ws2_32", "Bcrypt")
elseif is_os("linux") then 
    add_links("pthread")
    set_config("cxxflags", "-fPIC")
end

add_packages("spdlog", "websocketpp", "openssl", "sqlite3", "asio", "nlohmann_json")

includes("thirdparty")

target("log")
    set_kind("headeronly")
    add_packages("spdlog")
    add_headerfiles("src/log/log.h")
    add_includedirs("src/log", {public = true})

target("common")
    set_kind("headeronly")
    add_includedirs("src/common", {public = true})

target("device_db_manager")
    set_kind("object")
    add_deps("log")
    add_files("src/device_db_manager/*.cpp")
    add_includedirs("src/device_db_manager", {public = true})

target("transmission")
    set_kind("object")
    add_deps("log")
    add_files("src/transmission_manager.cpp")
    add_includedirs("src", {public = true})

target("negotiation")
    set_kind("object")
    add_deps("log", "transmission", "device_db_manager")
    add_files("src/signal_negotiation.cpp")
    add_includedirs("src", {public = true})

target("server")
    set_kind("object")
    add_deps("log", "common", "negotiation")
    add_files("src/signal_server.cpp")

target("crossdesk_server")
    set_kind("binary")
    add_deps("log", "common", "server")
    add_files("src/main.cpp")
