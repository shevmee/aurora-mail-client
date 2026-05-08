cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAIL_MESSAGE_SOURCES
    src/MailAttachment.cpp
    src/MailMessageBuilder.cpp
    src/MailAddress.cpp
    src/ReceivedMailMessage.cpp
)

add_library(MailMessage STATIC)

target_sources(MailMessage PRIVATE ${MAIL_MESSAGE_SOURCES})

target_include_directories(MailMessage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(MailMessage PUBLIC
    PkgConfig::gmime
)

# Build tests only if BUILD_TESTS is enabled and GTest is available
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
    set(MAIL_MESSAGE_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsMailAddress.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsMailMessageBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsMailAttachment.cpp
    )
    add_executable(MailMessageTests ${MAIL_MESSAGE_TEST_SOURCES})

    target_include_directories(MailMessageTests INTERFACE include)

    target_compile_features(MailMessageTests PRIVATE cxx_std_23)

    target_link_libraries(MailMessageTests PRIVATE
        MailMessage
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME MailMessageTests COMMAND MailMessageTests)
endif()
