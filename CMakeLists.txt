cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(experiments DESCRIPTION "Experiments" VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

file(GLOB_RECURSE headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS src/*.cpp src/*.manifest src/*.rc)

add_executable(${PROJECT_NAME} ${headers} ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00)
target_compile_options(${PROJECT_NAME} PRIVATE /await $<$<CONFIG:Release>:/await:heapelide>)

target_include_directories(${PROJECT_NAME} PRIVATE third_party/asio/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE ASIO_DISABLE_CONCEPTS ASIO_HAS_CO_AWAIT)

target_include_directories(${PROJECT_NAME} PRIVATE third_party/fmt/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE FMT_HEADER_ONLY)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
