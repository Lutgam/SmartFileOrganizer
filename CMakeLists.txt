cmake_minimum_required(VERSION 3.16)

project(SmartFileOrganizer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)
find_package(Qt6 COMPONENTS Pdf QUIET)
find_package(Qt6 COMPONENTS Test QUIET)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX)
    link_libraries(ws2_32)
endif()

# MSVC parses narrow string literals with the ANSI codepage by default; our
# sources contain UTF-8 CJK literals, so force UTF-8 source/execution charset.
if(MSVC)
    add_compile_options(/utf-8)
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
    GIT_TAG        b9587
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

# --- Core library: pure-Qt logic shared by the app, the tests and the eval runner ---
add_library(sfo_core STATIC
    src/core/FileScanner.cpp
    src/core/FileScanner.h
    src/core/TagManager.cpp
    src/core/TagManager.h
    src/core/DrawerCategoryLut.cpp
    src/core/DrawerCategoryLut.h
    src/core/AutoRuleEngine.cpp
    src/core/AutoRuleEngine.h
    src/core/DocumentParser.cpp
    src/core/DocumentParser.h
    src/gui/LanguageManager.cpp
    src/gui/LanguageManager.h
    src/ai/AiJsonSanitizer.cpp
    src/ai/AiJsonSanitizer.h
    ${miniz_SOURCE_DIR}/miniz.c
)

target_include_directories(sfo_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${miniz_SOURCE_DIR}
)

target_link_libraries(sfo_core PUBLIC Qt6::Core Qt6::Gui nlohmann_json::nlohmann_json)

# --- Optional PDF text extraction support (DocumentParser lives in sfo_core) ---
if (Qt6Pdf_FOUND)
    target_link_libraries(sfo_core PUBLIC Qt6::Pdf)
    target_compile_definitions(sfo_core PUBLIC HAVE_QT_PDF=1)
else()
    # Fallback: poppler-qt6 (if installed)
    find_package(PopplerQt6 QUIET)
    if (PopplerQt6_FOUND)
        target_link_libraries(sfo_core PUBLIC Poppler::Qt6)
        target_compile_definitions(sfo_core PUBLIC HAVE_POPPLER_QT6=1)
    endif()
endif()

# --- AI engine library (llama.cpp-backed; shared by app and eval runner) ---
add_library(sfo_ai STATIC
    src/ai/LlamaEngine.cpp
    src/ai/LlamaEngine.h
    src/ai/EmbeddingEngine.cpp
    src/ai/EmbeddingEngine.h
)

target_include_directories(sfo_ai PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/llama_cpp-src/vendor
    ${CMAKE_BINARY_DIR}/_deps/llama_cpp-src/common
)

target_link_libraries(sfo_ai PUBLIC sfo_core llama)

# --- Main application ---
add_executable(SmartFileOrganizer
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/SettingsDialog.cpp
    src/gui/SettingsDialog.h
    src/gui/RedundancyReportDialog.cpp
    src/gui/RedundancyReportDialog.h
    src/gui/ModelDownloadDialog.cpp
    src/gui/ModelDownloadDialog.h
    src/gui/RulesDialog.cpp
    src/gui/RulesDialog.h
    src/gui/GraphWidget.cpp
    src/gui/GraphWidget.h
)

target_link_libraries(SmartFileOrganizer PRIVATE
    sfo_core
    sfo_ai
    Qt6::Widgets Qt6::Concurrent Qt6::Network
)

# --- Headless evaluation runner (quantitative accuracy / throughput report) ---
add_executable(sfo_eval tools/EvalRunner.cpp)
target_link_libraries(sfo_eval PRIVATE sfo_core sfo_ai)

if(WIN32)
    set_property(TARGET SmartFileOrganizer PROPERTY WIN32_EXECUTABLE ON)
endif()

if(APPLE)
    # IOKit: battery / power-source detection for battery-aware batch pausing.
    target_link_libraries(SmartFileOrganizer PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

# --- Unit tests ---
if (Qt6Test_FOUND)
    enable_testing()

    function(sfo_add_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE sfo_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    sfo_add_test(test_tagmanager)
    sfo_add_test(test_drawerlut)
    sfo_add_test(test_documentparser)
    sfo_add_test(test_aijson)
    sfo_add_test(test_autorules)
endif()
