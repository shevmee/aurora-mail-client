cmake_minimum_required(VERSION 3.22)

# Add subdirectories
add_subdirectory(Smtp)
add_subdirectory(Imap)

# Create a unified ResponseHandling library that bundles both Imap and Smtp
add_library(ResponseHandling INTERFACE)

target_link_libraries(ResponseHandling INTERFACE
    ImapResponseHandling
    SmtpResponseHandling
)

