cmake_minimum_required(VERSION 3.25)
project(pi-coding-agent
    VERSION 1.0.0
    DESCRIPTION "C++20 coding agent with LLM provider support and i18n"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(CORE_SOURCES
    src/core/error.cpp
    src/core/config.cpp
    src/i18n/i18n.cpp
)
set(PROVIDER_SOURCES
    src/provider/provider.cpp
    src/provider/registry.cpp
    src/provider/openai.cpp
)
set(TOOL_SOURCES
    src/tools/tool.cpp
    src/tools/registry.cpp
    src/tools/read.cpp
    src/tools/edit.cpp
    src/tools/write.cpp
    src/tools/bash.cpp
    src/tools/search.cpp
)
set(SESSION_SOURCES
    src/session/message.cpp
    src/session/session.cpp
)
set(AGENT_SOURCES
    src/agent/agent.cpp
)
set(TUI_SOURCES
    src/tui/term.cpp
    src/tui/component.cpp
    src/tui/text.cpp
    src/tui/editor.cpp
    src/tui/markdown.cpp
    src/tui/status_line.cpp
    src/tui/tui.cpp
    src/tui/app.cpp
)

add_executable(pi-coding-agent
    src/main.cpp
    ${CORE_SOURCES}
    ${PROVIDER_SOURCES}
    ${TOOL_SOURCES}
    ${SESSION_SOURCES}
    ${AGENT_SOURCES}
    ${TUI_SOURCES}
)

target_include_directories(pi-coding-agent PRIVATE src)
target_link_libraries(pi-coding-agent PRIVATE
    nlohmann_json::nlohmann_json
    curl
    pthread
)

target_compile_definitions(pi-coding-agent PRIVATE
    PI_AGENT_VERSION="${PROJECT_VERSION}"
)

file(COPY locales DESTINATION ${CMAKE_BINARY_DIR})
install(TARGETS pi-coding-agent DESTINATION bin)
install(DIRECTORY locales/ DESTINATION share/pi-coding-agent/locales)
