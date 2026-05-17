set(IOCONTEXT_HEADERS
    IoContextRunner.hpp
)

set(IOCONTEXT_SOURCES
    IoContextRunner.cpp
)

add_library(AuroraMailIoContext STATIC
    ${IOCONTEXT_HEADERS}
    ${IOCONTEXT_SOURCES}
)

add_library(AuroraMail::IoContext ALIAS AuroraMailIoContext)

target_include_directories(AuroraMailIoContext
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AuroraMailIoContext
    PUBLIC
        Boost::headers
        Qt6::Core
)

target_compile_features(AuroraMailIoContext PUBLIC cxx_std_23)

# Enable all warnings for development
if(MSVC)
    target_compile_options(AuroraMailIoContext PRIVATE /W4)
else()
    target_compile_options(AuroraMailIoContext PRIVATE -Wall -Wextra -Wpedantic)
endif()
