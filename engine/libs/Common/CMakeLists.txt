add_subdirectory(Utils)
add_subdirectory(Logging)
add_subdirectory(Configs)
add_subdirectory(Stream)
add_subdirectory(MailMessage)
add_subdirectory(MessageCache)
add_subdirectory(MimeHandling)

# Create a unified Common library that bundles all common components
add_library(Common INTERFACE)

target_link_libraries(Common INTERFACE
    Utils
    Configs
    Logging
    Stream
    MailMessage
    IMessageCache
    InMemoryMessageCache
    MimeHandling
)
