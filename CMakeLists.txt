cmake_minimum_required(VERSION 3.16)

project(SmartFileOrganizer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
    link_libraries(ws2_32)
endif()

include(FetchContent)

# Set llama.cpp options before MakeAvailable
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp
    GIT_TAG        master
)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/releases/download/2.2.0/miniz-2.2.0.zip
)

FetchContent_MakeAvailable(llama_cpp json miniz)

add_executable(SmartFileOrganizer
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/DuplicateCleanerDialog.cpp
    src/gui/DuplicateCleanerDialog.h
    src/gui/GraphWidget.cpp
    src/gui/GraphWidget.h
    src/core/FileScanner.cpp
    src/core/FileScanner.h
    src/core/TagManager.cpp
    src/core/TagManager.h
    src/ai/LlamaEngine.cpp
    src/ai/LlamaEngine.h
    src/core/DocumentParser.cpp
    src/core/DocumentParser.h
    ${miniz_SOURCE_DIR}/miniz.c
)

target_include_directories(SmartFileOrganizer PRIVATE 
    ${CMAKE_BINARY_DIR}/_deps/llama_cpp-src/vendor
    ${CMAKE_BINARY_DIR}/_deps/llama_cpp-src/common
    ${miniz_SOURCE_DIR}
)

target_link_libraries(SmartFileOrganizer PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network llama nlohmann_json::nlohmann_json)

if(WIN32)
    set_property(TARGET SmartFileOrganizer PROPERTY WIN32_EXECUTABLE ON)
endif()
