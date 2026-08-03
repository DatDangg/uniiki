add_library(uniiki MODULE
    uniiki.cpp
    uniiki_engine.cpp
)

set_target_properties(uniiki PROPERTIES OUTPUT_NAME "uniiki")
target_link_libraries(uniiki Fcitx5::Core)
target_include_directories(uniiki PRIVATE ${PROJECT_BINARY_DIR})

install(TARGETS uniiki DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

configure_file(uniiki-addon.conf.in.in uniiki-addon.conf.in @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/uniiki-addon.conf.in"
        RENAME uniiki.conf
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

configure_file(uniiki.conf.in uniiki.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/uniiki.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")

add_executable(uniiki_engine_test
    uniiki_engine_test.cpp
    uniiki_engine.cpp
)
target_link_libraries(uniiki_engine_test Fcitx5::Core)

# Kept as a broad legacy regression executable, but intentionally not
# registered with CTest because it mixes converter and direct-commit checks.

add_executable(uniiki_converter_test
    uniiki_converter_test.cpp
    uniiki_engine.cpp
)
target_link_libraries(uniiki_converter_test Fcitx5::Core)
add_test(NAME uniiki_converter_test COMMAND uniiki_converter_test)
set_tests_properties(uniiki_converter_test PROPERTIES LABELS "converter")

add_executable(uniiki_direct_commit_test
    uniiki_direct_commit_test.cpp
    uniiki_engine.cpp
)
target_link_libraries(uniiki_direct_commit_test Fcitx5::Core)
add_test(NAME uniiki_direct_commit_test COMMAND uniiki_direct_commit_test)
set_tests_properties(uniiki_direct_commit_test PROPERTIES LABELS "direct-commit")

add_executable(uniiki_availability_test
    uniiki_availability_test.cpp
    uniiki_engine.cpp
)
target_link_libraries(uniiki_availability_test Fcitx5::Core)
add_test(NAME uniiki_availability_test COMMAND uniiki_availability_test)
set_tests_properties(uniiki_availability_test PROPERTIES
    LABELS "availability;fuzz"
    TIMEOUT 180)
