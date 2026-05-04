cmake_minimum_required(VERSION 3.20)

project(
    cpp_rag
    VERSION 1.0.0
    DESCRIPTION "C++ Retrieval-Augmented Generation with Turso + Ollama"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Build type default
# ---------------------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------

if(MSVC)
    add_compile_options(/W4 /WX- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Dependencies: libcurl (system)
# ---------------------------------------------------------------------------

find_package(CURL REQUIRED)

# ---------------------------------------------------------------------------
# Dependencies: OpenSSL (system)
# ---------------------------------------------------------------------------

find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Dependencies: nlohmann/json (FetchContent)
# ---------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Shared library of common code
# ---------------------------------------------------------------------------

# All sources except the four main() files go into a static library so each
# executable links exactly one copy of the shared code.

add_library(rag_core STATIC
    src/http.cpp
    src/turso.cpp
    src/ollama.cpp
    src/chunker.cpp
    src/parser.cpp
)

target_include_directories(rag_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rag_core
    PUBLIC
        CURL::libcurl
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
)

# On Windows, OpenSSL often needs ws2_32.
if(WIN32)
    target_link_libraries(rag_core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# Executable: ingest
# ---------------------------------------------------------------------------

add_executable(ingest src/ingest.cpp)
target_link_libraries(ingest PRIVATE rag_core)

# ---------------------------------------------------------------------------
# Executable: query
# ---------------------------------------------------------------------------

add_executable(query src/query.cpp)
target_link_libraries(query PRIVATE rag_core)

# ---------------------------------------------------------------------------
# Executable: rag
# ---------------------------------------------------------------------------

add_executable(rag src/rag.cpp)
target_link_libraries(rag PRIVATE rag_core)

# ---------------------------------------------------------------------------
# Executable: chatbot
# ---------------------------------------------------------------------------

add_executable(chatbot src/chatbot.cpp)
target_link_libraries(chatbot PRIVATE rag_core)

# ---------------------------------------------------------------------------
# Install rules (optional — for cmake --install)
# ---------------------------------------------------------------------------

install(TARGETS ingest query rag chatbot
    RUNTIME DESTINATION bin
)
