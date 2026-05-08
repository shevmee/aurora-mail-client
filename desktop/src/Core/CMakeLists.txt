# OAuth Credentials Configuration
# Credentials can be set via:
#   1. CMake: cmake -DOAUTH_CLIENT_ID=xxx -DOAUTH_CLIENT_SECRET=xxx ..
#   2. Environment at build time: AURORA_OAUTH_CLIENT_ID, AURORA_OAUTH_CLIENT_SECRET
#   3. Environment at runtime (fallback)

if(NOT DEFINED OAUTH_CLIENT_ID)
    set(OAUTH_CLIENT_ID "$ENV{AURORA_OAUTH_CLIENT_ID}")
endif()

if(NOT DEFINED OAUTH_CLIENT_SECRET)
    set(OAUTH_CLIENT_SECRET "$ENV{AURORA_OAUTH_CLIENT_SECRET}")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Config/OAuthCredentials.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/Config/OAuthCredentials.hpp
    @ONLY
)

if(OAUTH_CLIENT_ID AND OAUTH_CLIENT_SECRET)
    message(STATUS "OAuth: credentials configured (compile-time)")
elseif(OAUTH_CLIENT_ID)
    message(STATUS "OAuth: client ID set, secret missing")
else()
    message(STATUS "OAuth: not configured (will use runtime environment)")
endif()

add_subdirectory(Auth)
add_subdirectory(AI)
add_subdirectory(IoContext)

set(CORE_SOURCES
    # Desktop application configuration. Owns JSON parsing for the desktop's
    # config.json (the engine library is JSON-agnostic by design — see
    # engine/libs/Common/Configs/include/StartupConfig.hpp). AppConfig adds
    # `locale` so the UI translator can switch to Ukrainian (NFR-10).
    Config/AppConfig.hpp
    Config/AppConfig.cpp

    Email/EmailParser.hpp
    Email/EmailParser.cpp
    
    Utils/TextSanitizer.hpp
    Utils/TextSanitizer.cpp

    Mail/ImapSessionTypes.hpp
    Mail/ImapCommandQueue.hpp
    Mail/ImapCommandQueue.cpp
    Mail/ImapSessionController.hpp
    Mail/ImapSessionController.cpp
    Mail/MailSessionService.hpp
    Mail/MailSessionService.cpp
    Mail/MailSessionSignals.hpp
    Mail/MailSessionSignals.cpp

    # Layered message body cache: keyed on (account, mailbox, UIDVALIDITY, uid).
    # Tier 1: MemoryMessageCache (single-map LRU + byte budget).
    # Tier 2: PersistentMessageCache (per-account SQLite, AES-256-GCM under a
    #         keychain-resident master key — see CacheKeyMaterial).
    # Composed by TieredMessageCache behind the IMessageCache interface.
    Mail/Cache/MessageKey.hpp
    Mail/Cache/CachedMessage.hpp
    Mail/Cache/CachedMessage.cpp
    Mail/Cache/IMessageCache.hpp
    Mail/Cache/MemoryMessageCache.hpp
    Mail/Cache/MemoryMessageCache.cpp
    Mail/Cache/AesGcmCipher.hpp
    Mail/Cache/AesGcmCipher.cpp
    Mail/Cache/CacheKeyMaterial.hpp
    Mail/Cache/CacheKeyMaterial.cpp
    Mail/Cache/PersistentMessageCache.hpp
    Mail/Cache/PersistentMessageCache.cpp
    Mail/Cache/TieredMessageCache.hpp
    Mail/Cache/TieredMessageCache.cpp
    
    ${CMAKE_CURRENT_BINARY_DIR}/Config/OAuthCredentials.hpp
)

add_library(AuroraMailCore STATIC ${CORE_SOURCES})

# Qt SQL drives the per-account encrypted SQLite tier of the message cache
# (Mail/Cache/PersistentMessageCache). It ships with the standard Qt install.
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Sql)

target_include_directories(AuroraMailCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Email
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils
        ${CMAKE_CURRENT_SOURCE_DIR}/Mail
        ${CMAKE_CURRENT_SOURCE_DIR}/Mail/Cache
        ${CMAKE_CURRENT_BINARY_DIR}/Config
)

target_link_libraries(AuroraMailCore
    PUBLIC
        AuroraMail::Auth
        AuroraMail::IoContext
        AuroraMail::AI
        AuroraMailEngine
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Network
        Boost::headers
)

# Private-link dependencies of AuroraMailCore:
#   - OpenSSL::Crypto: AES-256-GCM AEAD for the at-rest body cache (NFR-05).
#   - Qt::Sql:         SQLite-backed per-account on-disk message-cache tier.
# JSON parsing (AppConfig, OAuth tokens, account registry, AI REST) is done
# through Qt's QJsonDocument family — nlohmann_json is no longer linked here.
target_link_libraries(AuroraMailCore PRIVATE
    OpenSSL::Crypto
    Qt${QT_VERSION_MAJOR}::Sql
)

# On Windows, PersistentMessageCache narrows its DACL to the current user via
# SetNamedSecurityInfo / SetEntriesInAcl from advapi32, mirroring the POSIX
# `chmod 0600` we apply elsewhere (NFR-05). MinGW and MSVC both link advapi32
# implicitly in most templates, but be explicit so this can never silently
# regress on a vendored toolchain.
if(WIN32)
    target_link_libraries(AuroraMailCore PRIVATE advapi32)
endif()
