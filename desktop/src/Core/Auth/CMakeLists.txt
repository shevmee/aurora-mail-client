set(AUTH_PUBLIC_HEADERS
    OAuthConfig.hpp
    OAuthManager.hpp
    TokenStorage.hpp
    PasswordCredentialsStorage.hpp
    AccountRegistry.hpp
)

set(AUTH_PUBLIC_SOURCES
    OAuthManager.cpp
)

set(AUTH_INTERNAL_HEADERS
    Internal/PkceSession.hpp
    Internal/AuthPageRenderer.hpp
    Internal/OAuthRedirectServer.hpp
)

set(AUTH_INTERNAL_SOURCES
    Internal/PkceSession.cpp
    Internal/AuthPageRenderer.cpp
    Internal/OAuthRedirectServer.cpp
)

set(AUTH_STORAGE_HEADERS
    Storage/ISecureStorage.hpp
    Storage/SettingsBackend.hpp
)

set(AUTH_STORAGE_SOURCES
    Storage/SettingsBackend.cpp
)

# Probe for QtKeychain when keychain is requested. If it's not installed,
# downgrade to QSettings rather than hard-failing the build.
set(_AURORA_KEYCHAIN_AVAILABLE OFF)
if(USE_SYSTEM_KEYCHAIN)
    if(QT_VERSION_MAJOR EQUAL 6)
        find_package(Qt6Keychain QUIET)
        if(Qt6Keychain_FOUND)
            set(_AURORA_KEYCHAIN_AVAILABLE ON)
        endif()
    else()
        find_package(Qt5Keychain QUIET)
        if(Qt5Keychain_FOUND)
            set(_AURORA_KEYCHAIN_AVAILABLE ON)
        endif()
    endif()
endif()

if(_AURORA_KEYCHAIN_AVAILABLE)
    list(APPEND AUTH_STORAGE_HEADERS Storage/KeychainBackend.hpp)
    list(APPEND AUTH_STORAGE_SOURCES Storage/KeychainBackend.cpp)
endif()

set(AUTH_RESOURCES
    Resources/oauth_resources.qrc
)

# Combine all sources
set(AUTH_SOURCES
    ${AUTH_PUBLIC_HEADERS}
    ${AUTH_PUBLIC_SOURCES}
    ${AUTH_INTERNAL_HEADERS}
    ${AUTH_INTERNAL_SOURCES}
    ${AUTH_STORAGE_HEADERS}
    ${AUTH_STORAGE_SOURCES}
    ${AUTH_RESOURCES}
)

# Create the Auth library
add_library(AuroraMailAuth STATIC ${AUTH_SOURCES})

add_library(AuroraMail::Auth ALIAS AuroraMailAuth)

target_include_directories(AuroraMailAuth
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Storage
        ${CMAKE_CURRENT_SOURCE_DIR}/Internal
)

target_link_libraries(AuroraMailAuth PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Gui
)

if(_AURORA_KEYCHAIN_AVAILABLE)
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(AuroraMailAuth PUBLIC Qt6Keychain::Qt6Keychain)
    else()
        target_link_libraries(AuroraMailAuth PUBLIC Qt5Keychain::Qt5Keychain)
    endif()
    target_compile_definitions(AuroraMailAuth PUBLIC AURORA_USE_KEYCHAIN=1)
    message(STATUS "Auth: Using QtKeychain for secure credential storage")
else()
    target_compile_definitions(AuroraMailAuth PUBLIC AURORA_USE_KEYCHAIN=0)
    if(USE_SYSTEM_KEYCHAIN)
        message(WARNING
            "Auth: USE_SYSTEM_KEYCHAIN=ON but QtKeychain was not found. "
            "Falling back to QSettings (development mode - NOT SECURE). "
            "Install QtKeychain (e.g. brew install qtkeychain) to enable secure storage.")
    else()
        message(STATUS "Auth: Using QSettings for credential storage (development mode - NOT SECURE)")
    endif()
endif()
