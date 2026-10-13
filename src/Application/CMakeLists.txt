cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

set(APPLICATION_SOURCES
        Game.cpp
        GameConfig.cpp
        GameMenu.cpp
        GameOver.cpp
        GameTraceHandler.cpp
        GameWindowHandler.cpp)

set(APPLICATION_HEADERS
        Game.h
        GameConfig.h
        GameMenu.h
        GameOver.h
        GameTraceHandler.h
        GameWindowHandler.h
        KeyConfigEntry.h)

add_library(application STATIC ${APPLICATION_SOURCES} ${APPLICATION_HEADERS})
target_check_style(application)

target_link_libraries(application
        PUBLIC
        engine
        engine_components_control
        engine_components_trace
        engine_graphics
        engine_graphics_renderer
        application_game_states
        application_startup
        core_serialization
        gui
        io
        library_config
        library_fsm
        library_lod
        media
        library_platform_application
        library_platform_interface
        scripting
        utility
        imgui)

add_subdirectory(GameStates)
add_subdirectory(Startup)

if(OE_BUILD_TESTS)
    set(TEST_APPLICATION_SOURCES
            Tests/GameConfig_ut.cpp)

    add_library(test_application OBJECT ${TEST_APPLICATION_SOURCES})
    target_link_libraries(test_application PUBLIC testing_unit application)

    target_check_style(test_application)

    target_link_libraries(OpenEnroth_GameTest PUBLIC test_application)
endif()
