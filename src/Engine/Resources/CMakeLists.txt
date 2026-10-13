cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

set(ENGINE_RESOURCES_SOURCES
        EngineFileSystem.cpp
        GiProbes.cpp
        LOD.cpp
        LodSpriteCache.cpp
        ResourceManager.cpp
        TexturePack.cpp
        TileGenerator.cpp)

set(ENGINE_RESOURCES_HEADERS
        EngineFileSystem.h
        GiProbes.h
        LOD.h
        LodSpriteCache.h
        ResourceManager.h
        TexturePack.h
        TileGenerator.h)

add_library(engine_resources STATIC ${ENGINE_RESOURCES_SOURCES} ${ENGINE_RESOURCES_HEADERS})
target_link_libraries(engine_resources PUBLIC engine_tables library_compression library_image library_json library_lod library_lod_formats library_logger library_filesystem_interface utility)
target_check_style(engine_resources)

if(OE_BUILD_TESTS)
    set(TEST_ENGINE_RESOURCES_SOURCES
            Tests/GiProbes_ut.cpp
            Tests/TexturePack_ut.cpp)

    add_library(test_engine_resources OBJECT ${TEST_ENGINE_RESOURCES_SOURCES})
    target_link_libraries(test_engine_resources PUBLIC testing_unit engine_resources library_filesystem_memory)

    target_check_style(test_engine_resources)

    target_link_libraries(OpenEnroth_UnitTest PUBLIC test_engine_resources)
endif()
