cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(BaseClient STATIC)

target_sources(BaseClient PRIVATE 
    src/BaseProtocolClient.cpp
)

target_include_directories(BaseClient PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(BaseClient PUBLIC
    Stream
    MailMessage
    Utils
    Logging
)

# Build tests only if BUILD_TESTS is enabled and GTest is available
# if(BUILD_TESTS AND GTest_FOUND)
#     # BaseProtocolClient tests
#     if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsBaseProtocolClient.cpp)
#         add_executable(BaseProtocolClientTests test/TestsBaseProtocolClient.cpp)

#         target_include_directories(BaseProtocolClientTests INTERFACE include)

#         target_compile_features(BaseProtocolClientTests PRIVATE cxx_std_23)
        
#         target_link_libraries(BaseProtocolClientTests PRIVATE
#             BaseClient
#             GTest::gtest
#             GTest::gtest_main
#         )

#         if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#             target_compile_options(BaseProtocolClientTests PRIVATE
#                 -fprofile-instr-generate
#                 -fcoverage-mapping
#             )
#             target_link_libraries(BaseProtocolClientTests PRIVATE
#                 -fprofile-instr-generate
#             )
#         endif()

#         add_test(NAME BaseProtocolClientTests COMMAND BaseProtocolClientTests)
#     endif()

#     # AttachmentManager tests
#     if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsAttachmentManager.cpp)
#         add_executable(AttachmentManagerTests test/TestsAttachmentManager.cpp)

#         target_include_directories(AttachmentManagerTests INTERFACE include)

#         target_compile_features(AttachmentManagerTests PRIVATE cxx_std_23)
        
#         target_link_libraries(AttachmentManagerTests PRIVATE
#             BaseClient
#             GTest::gtest
#             GTest::gtest_main
#         )

#         if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#             target_compile_options(AttachmentManagerTests PRIVATE
#                 -fprofile-instr-generate
#                 -fcoverage-mapping
#             )
#             target_link_libraries(AttachmentManagerTests PRIVATE
#                 -fprofile-instr-generate
#             )
#         endif()

#         add_test(NAME AttachmentManagerTests COMMAND AttachmentManagerTests)
#     endif()
# endif()