cmake_minimum_required(VERSION 3.21)
project(flux-encoder VERSION 0.8.18 LANGUAGES CXX)
if(NOT DEFINED FLUX_REPOSITORY_ROOT)
    get_filename_component(FLUX_REPOSITORY_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
set(FLUX_SUITE_COMMON_DIR "${FLUX_REPOSITORY_ROOT}/packages/flux-common"
    CACHE PATH "Flux Suite shared sources and resources")
include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
                  FLUX_ENCODER_VERSION_LABEL)
flux_extract_semver("${FLUX_ENCODER_VERSION_LABEL}"
                    FLUX_ENCODER_SEMANTIC_VERSION)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/FS)
endif()

option(FLUX_ENCODER_BUILD_TESTS "Build Flux Encoder tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Concurrent Svg Network)

add_library(flux-encoder-core STATIC
    src/core/media-types.cpp
    src/core/database.cpp
    src/core/ffmpeg-capabilities.cpp
    src/core/profile-catalog.cpp
    src/core/job-builder.cpp
    src/core/queue-runner.cpp
    src/providers/provider-catalog.cpp
    src/providers/provider-process.cpp
    src/providers/render-source-descriptor.cpp
    src/providers/render-provider-support.cpp
)
target_include_directories(flux-encoder-core PUBLIC src src/provider-sdk)
target_link_libraries(flux-encoder-core PUBLIC Qt6::Core Qt6::Sql Qt6::Concurrent)
target_compile_definitions(flux-encoder-core PUBLIC
    FLUX_ENCODER_VERSION="${FLUX_ENCODER_VERSION_LABEL}"
    FLUX_ENCODER_SEMANTIC_VERSION="${FLUX_ENCODER_SEMANTIC_VERSION}"
)

add_library(flux-encoder-widgets STATIC
    src/ui/flux/flux-modern-controls.cpp
    src/ui/flux/flux-theme.cpp
    src/ui/flux-branding.cpp
)
target_include_directories(flux-encoder-widgets PUBLIC src)
target_link_libraries(flux-encoder-widgets PUBLIC Qt6::Core Qt6::Widgets Qt6::Svg)

qt_add_resources(FLUX_ENCODER_RESOURCES resources/resources.qrc)

add_executable(flux-encoder
    src/app/main.cpp
    src/app/main-window.cpp
    src/app/export-ipc-server.cpp
    src/app/profile-editor-dialog.cpp
    src/app/metadata-export-dialog.cpp
    ${FLUX_ENCODER_RESOURCES}
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/resources/flux-encoder.rc>
)
target_link_libraries(flux-encoder PRIVATE flux-encoder-core flux-encoder-widgets Qt6::Widgets Qt6::Network)
set_target_properties(flux-encoder PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "flux-encoder"
)
if(WIN32)
    set_target_properties(flux-encoder PROPERTIES OUTPUT_NAME "Flux Encoder")
endif()

add_executable(flux-encoder-worker
    src/worker/main.cpp
    src/worker/worker-application.cpp
)
target_link_libraries(flux-encoder-worker PRIVATE flux-encoder-core Qt6::Core)

if(WIN32)
    target_compile_definitions(flux-encoder-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(flux-encoder-widgets PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)
install(TARGETS flux-encoder flux-encoder-worker
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${FLUX_REPOSITORY_ROOT}/LICENSE" README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY providers/ DESTINATION providers OPTIONAL)
install(DIRECTORY src/provider-sdk/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/flux-encoder/provider-sdk FILES_MATCHING PATTERN "*.h")

if(FLUX_ENCODER_BUILD_TESTS)
    enable_testing()
    add_executable(flux-encoder-profile-contract-test tests/profile-contract-test.cpp)
    target_link_libraries(flux-encoder-profile-contract-test PRIVATE flux-encoder-core Qt6::Core)
    add_test(NAME flux-encoder-profile-contract-test COMMAND flux-encoder-profile-contract-test)
    add_executable(flux-encoder-provider-sdk-contract-test tests/provider-sdk-contract-test.cpp)
    target_link_libraries(flux-encoder-provider-sdk-contract-test PRIVATE flux-encoder-core Qt6::Core)
    add_test(NAME flux-encoder-provider-sdk-contract-test COMMAND flux-encoder-provider-sdk-contract-test)
    add_executable(flux-encoder-export-ipc-contract-test
        tests/export-ipc-contract-test.cpp
        src/app/export-ipc-server.cpp)
    target_include_directories(flux-encoder-export-ipc-contract-test PRIVATE src)
    target_link_libraries(flux-encoder-export-ipc-contract-test PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME flux-encoder-export-ipc-contract-test COMMAND flux-encoder-export-ipc-contract-test)
    add_executable(flux-encoder-startup-ipc-preset-test
        tests/startup-ipc-preset-test.cpp
        src/app/main-window.cpp
        src/app/profile-editor-dialog.cpp
        src/app/metadata-export-dialog.cpp
        ${FLUX_ENCODER_RESOURCES})
    target_include_directories(flux-encoder-startup-ipc-preset-test PRIVATE src)
    target_link_libraries(flux-encoder-startup-ipc-preset-test PRIVATE
        flux-encoder-core flux-encoder-widgets Qt6::Widgets Qt6::Network)
    add_test(NAME flux-encoder-startup-ipc-preset-test
        COMMAND flux-encoder-startup-ipc-preset-test)
    set_tests_properties(flux-encoder-startup-ipc-preset-test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30)
endif()
