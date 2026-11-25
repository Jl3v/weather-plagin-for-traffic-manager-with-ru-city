cmake_minimum_required(VERSION 3.16)
project(WeatherRU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(WeatherRU SHARED
    src/WeatherClient.h
    src/WeatherClient.cpp
    src/WeatherRUPlugin.h
    src/WeatherRUPlugin.cpp
    src/resource.h
    src/resource.rc
)

target_include_directories(WeatherRU PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(WeatherRU PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if (MSVC)
    target_compile_options(WeatherRU PRIVATE /utf-8 /W3)
    set_property(TARGET WeatherRU PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(WeatherRU PRIVATE winhttp user32 gdi32 gdiplus shell32)
