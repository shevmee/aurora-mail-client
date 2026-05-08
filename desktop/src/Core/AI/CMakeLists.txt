set(AI_HEADERS
    AIService.hpp
    AiApiKeyStorage.hpp
    GeminiProvider.hpp
)

set(AI_SOURCES
    AIService.cpp
    AiApiKeyStorage.cpp
    GeminiProvider.cpp
)

add_library(AuroraMailAI STATIC
    ${AI_HEADERS}
    ${AI_SOURCES}
)

add_library(AuroraMail::AI ALIAS AuroraMailAI)

target_include_directories(AuroraMailAI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AuroraMailAI
    PUBLIC
        AuroraMail::Auth
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
)

target_compile_features(AuroraMailAI PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(AuroraMailAI PRIVATE /W4)
else()
    target_compile_options(AuroraMailAI PRIVATE -Wall -Wextra -Wpedantic)
endif()
