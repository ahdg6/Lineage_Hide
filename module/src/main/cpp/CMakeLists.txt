cmake_minimum_required(VERSION 3.22.1)
project(LineageHide)

add_library(lineage_hide SHARED
    config.cpp
    config_transport.cpp
    entry.cpp
    service_filter.cpp
    service_cache.cpp
    binder_hook.cpp
    resource_hook.cpp
    reflection_filter.cpp
    feature_filter.cpp
    broadcast_filter.cpp
    log.cpp
)

target_include_directories(lineage_hide PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(lineage_hide PRIVATE
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
)

target_link_libraries(lineage_hide
    log
    dl
)

# Keep only the Zygisk entry point in .dynsym and remove the regular ELF
# symbol table from release artifacts so helper names cannot be enumerated.
target_link_options(lineage_hide PRIVATE
    "-Wl,--gc-sections"
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
)

if (CMAKE_STRIP)
    add_custom_command(TARGET lineage_hide POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:lineage_hide>
        COMMENT "Stripping private Lineage Hide symbols"
    )
endif()

set_target_properties(lineage_hide PROPERTIES
    OUTPUT_NAME "lineage_hide"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
target_compile_features(lineage_hide PRIVATE cxx_std_20)
add_executable(configctl configctl.cpp config.cpp susfs_backend.cpp)
target_compile_options(configctl PRIVATE -Wall -Wextra -fvisibility=hidden)
if (CMAKE_STRIP)
    add_custom_command(TARGET configctl POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:configctl>)
endif()
