cmake_minimum_required(VERSION 3.16)
project(fcitx5-uniiki VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM 5.70.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(Fcitx5Core REQUIRED)
find_package(Gettext REQUIRED)

include(GNUInstallDirs)
include(FeatureSummary)

add_library(uniiki MODULE
    src/engine.cpp
    src/uniiki_addon.cpp
)

target_link_libraries(uniiki PRIVATE Fcitx5::Core)
target_include_directories(uniiki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(uniiki PROPERTIES PREFIX "")

configure_file(src/uniiki-addon.conf.in ${CMAKE_CURRENT_BINARY_DIR}/uniiki.conf @ONLY)

install(TARGETS uniiki DESTINATION ${CMAKE_INSTALL_LIBDIR}/fcitx5)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/uniiki.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
