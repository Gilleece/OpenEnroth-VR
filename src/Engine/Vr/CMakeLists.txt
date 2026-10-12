cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

set(ENGINE_VR_SOURCES
        VrSystem.cpp)

set(ENGINE_VR_HEADERS
        VrSystem.h)

add_library(engine_vr STATIC ${ENGINE_VR_SOURCES} ${ENGINE_VR_HEADERS})
target_check_style(engine_vr)

target_link_libraries(engine_vr
        PUBLIC
        engine
        engine_graphics
        engine_graphics_renderer
        library_platform_proxy
        openxr_loader
        PRIVATE
        glad)

target_compile_definitions(engine_vr PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_OPENGL)
