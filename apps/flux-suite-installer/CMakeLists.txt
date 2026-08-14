cmake_minimum_required(VERSION 3.24)

project(flux-suite-installer VERSION 0.8.19 LANGUAGES CXX)

if(NOT DEFINED FLUX_REPOSITORY_ROOT)
    get_filename_component(FLUX_REPOSITORY_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
set(FLUX_SUITE_COMMON_DIR "${FLUX_REPOSITORY_ROOT}/packages/flux-common"
    CACHE PATH "Flux Suite shared sources and resources")
include("${FLUX_REPOSITORY_ROOT}/cmake/FluxVersion.cmake")
flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"
                  FLUX_INSTALLER_VERSION_LABEL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Svg Widgets)

qt_add_executable(flux-suite-installer WIN32
    src/main.cpp
    src/product.h
    src/manifest.h
    src/manifest.cpp
    src/network-utils.h
    src/network-utils.cpp
    src/update-security.h
    src/update-security.cpp
    src/selfupdater.h
    src/selfupdater.cpp
    src/fileassociations.h
    src/fileassociations.cpp
    src/installengine.h
    src/installengine.cpp
    src/installerwindow.h
    src/installerwindow.cpp
    resources/resources.qrc
    resources/flux-installer.rc
)

target_link_libraries(flux-suite-installer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Svg
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(flux-suite-installer PRIVATE bcrypt advapi32 shell32)
endif()

target_compile_definitions(flux-suite-installer PRIVATE
    FLUX_INSTALLER_VERSION="${FLUX_INSTALLER_VERSION_LABEL}"
    QT_NO_KEYWORDS
)

if(MSVC)
    target_compile_options(flux-suite-installer PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(flux-suite-installer PROPERTIES
    OUTPUT_NAME "Flux Suite"
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME manifest-validation
        COMMAND flux-suite-installer --validate-manifest
    )
    add_test(NAME update-signature-validation
        COMMAND flux-suite-installer --verify-feed
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/signed-feed.json"
    )
endif()
