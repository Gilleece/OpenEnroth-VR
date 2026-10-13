cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(GAME_TEST_MAIN_SOURCES
        GameTestCommon.cpp
        GameTestMain.cpp
        GameTestOptions.cpp
        GameTests_0000.cpp
        GameTests_0500.cpp
        GameTests_1000.cpp
        GameTests_1500.cpp
        GameTests_2000.cpp
        GameTests_2500.cpp)
set(GAME_TEST_MAIN_HEADERS
        GameTestCommon.h
        GameTestOptions.h)

add_executable(OpenEnroth_GameTest ${GAME_TEST_MAIN_SOURCES} ${GAME_TEST_MAIN_HEADERS})
target_link_libraries(OpenEnroth_GameTest PUBLIC application testing_game library_cli library_platform_main library_stack_trace)

target_check_style(OpenEnroth_GameTest)

add_custom_target(Run_GameTest
        OpenEnroth_GameTest --test-path ${OE_TESTDATA_PATH}
        DEPENDS OpenEnroth_GameTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL) # USES_TERMINAL makes the command print progress as it goes.

add_custom_target(Run_GameTest_Headless
        OpenEnroth_GameTest --test-path ${OE_TESTDATA_PATH} --headless
        DEPENDS OpenEnroth_GameTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL)

add_custom_target(Run_GameTest_Parallel
        Python::Interpreter ${CMAKE_SOURCE_DIR}/thirdparty/gtest_parallel/gtest-parallel --print_test_times
            $<TARGET_FILE:OpenEnroth_GameTest> -- --test-path ${OE_TESTDATA_PATH}
        DEPENDS OpenEnroth_GameTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL)

add_custom_target(Run_GameTest_Headless_Parallel
        Python::Interpreter ${CMAKE_SOURCE_DIR}/thirdparty/gtest_parallel/gtest-parallel --print_test_times
            $<TARGET_FILE:OpenEnroth_GameTest> -- --test-path ${OE_TESTDATA_PATH} --headless
        DEPENDS OpenEnroth_GameTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL)

if(OE_BUILD_VR)
    target_sources(OpenEnroth_GameTest PRIVATE VrSmokeTest.cpp)

    # Runs the VrSmoke suite flat and then in a headset session on the OpenXR runtime that OPENENROTH_VR_SMOKE names.
    set(VR_SMOKE_BASELINE ${CMAKE_CURRENT_BINARY_DIR}/vr_smoke_flat_warnings.txt)
    add_custom_target(Run_VrSmoke
            ${CMAKE_COMMAND} -E env OPENENROTH_VR_SMOKE_PASS=flat OPENENROTH_VR_SMOKE_BASELINE=${VR_SMOKE_BASELINE}
                $<TARGET_FILE:OpenEnroth_GameTest> --test-path ${OE_TESTDATA_PATH} --gtest_filter=VrSmoke.*
            COMMAND ${CMAKE_COMMAND} -E env OPENENROTH_VR_SMOKE_PASS=vr OPENENROTH_VR_SMOKE_BASELINE=${VR_SMOKE_BASELINE}
                $<TARGET_FILE:OpenEnroth_GameTest> --test-path ${OE_TESTDATA_PATH} --gtest_filter=VrSmoke.*
            DEPENDS OpenEnroth_GameTest
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            USES_TERMINAL)
endif()
