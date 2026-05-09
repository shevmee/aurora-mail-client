# Aurora Mail desktop application (Qt). Included from repository root CMakeLists.txt.

# Qt specific settings
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Sql)

# LinguistTools is optional: it is only needed at *build* time to update or
# release translation files. Without it the app still runs (it just falls back
# to the embedded English source strings via Qt's default tr() lookup), and
# the existing aurora-mail_uk.qm — if shipped — keeps working unchanged.
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS LinguistTools)

# Secure token / credential storage option (applied in src/Core/Auth/CMakeLists.txt).
# Default ON for platforms where QtKeychain is reliably available (macOS Keychain,
# Windows Credential Store, libsecret/KWallet on Linux). The CMakeLists in
# src/Core/Auth gracefully degrades to QSettings if Qt6Keychain is not found.
if(APPLE OR WIN32 OR UNIX)
    set(_AURORA_KEYCHAIN_DEFAULT ON)
else()
    set(_AURORA_KEYCHAIN_DEFAULT OFF)
endif()
option(USE_SYSTEM_KEYCHAIN
    "Use secure system keychain via QtKeychain (falls back to QSettings if not found)"
    ${_AURORA_KEYCHAIN_DEFAULT})

find_package(OpenSSL REQUIRED)
# 1.83: common on current distros; raise if your Boost lacks required Asio features.
find_package(Boost 1.83.0 REQUIRED CONFIG COMPONENTS headers)
# JSON parsing in the desktop tree is done via Qt's QJsonDocument family
# (AppConfig, OAuth tokens, account registry, AI REST). nlohmann_json is no
# longer required to build the desktop app; it is still pulled in at the
# top-level project for the engine CLI utility, which lives under engine/.

# Windows: ensure UTF-8 source/exec encoding for translated Ukrainian strings.
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(src)
add_subdirectory(test)

# ----------------------------------------------------------------------------
#
# `tr()` calls (and the <string> elements that uic auto-wraps from .ui forms)
# are scanned by lupdate and resolved at runtime by a QTranslator that is
# installed in main.cpp before any UI is shown. The .qm files produced by
# lrelease are embedded into the application via a generated .qrc file so the
# binary is self-contained — there is no need for a translations/ directory
# alongside the executable.
# ----------------------------------------------------------------------------
set(AURORA_TS_FILES
    translations/aurora-mail_uk.ts
)

set(AURORA_QM_FILES "")
if(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND AND ${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # Compile committed .ts → .qm. The .qm files are then plumbed into the
    # binary explicitly via qt_add_resources() (NOT AUTORCC, which would run
    # before the lrelease custom command produces the .qm output).
    #
    # Qt aggregates per-project lrelease invocations under the global
    # `release_translations` target, which is what we depend on below.
    # Refreshing the .ts entries from source is a developer-side workflow:
    #   lupdate -source-language en -target-language uk \
    #           desktop/src -ts desktop/translations/aurora-mail_uk.ts
    qt_add_lrelease(
        TS_FILES ${AURORA_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE AURORA_QM_FILES
    )
elseif(NOT Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
    message(STATUS
        "Qt LinguistTools not found — translations will not be (re)compiled."
        " The desktop app falls back to the embedded English source strings."
        " Install qt6-tools-dev (Linux) / qt6-base via Homebrew to enable.")
endif()

set(RESOURCES
    Resources/resources.qrc
    src/Core/Auth/Resources/oauth_resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(aurora-mail
        MANUAL_FINALIZATION
        main.cpp
        ${RESOURCES}
    )
else()
    add_executable(aurora-mail
        main.cpp
        ${RESOURCES}
    )
endif()

if(APPLE)
    target_sources(aurora-mail PRIVATE MacNativeCursorFilter.mm)
endif()

# Embed compiled .qm files into the binary under ":/translations/<basename>.qm",
# matching the prefix used by main.cpp's QTranslator::load(":/translations").
# qt_add_resources() (rather than AUTORCC) is required here because the .qm
# files are produced by Qt's `release_translations` aggregator target during
# the build — AUTORCC scans .qrc inputs at *configure* time, before lrelease
# has had a chance to run.
if(AURORA_QM_FILES)
    qt_add_resources(aurora-mail "translations"
        PREFIX "/translations"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${AURORA_QM_FILES}
    )
    if(TARGET release_translations)
        add_dependencies(aurora-mail release_translations)
    endif()
endif()

# Include directories
target_include_directories(aurora-mail PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link against our modules and dependencies
target_link_libraries(aurora-mail PRIVATE
    AuroraMailUi
    AuroraMailCore
    AuroraMailEngine
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::headers
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
)

# Windows-only system libs:
#   crypt32  – CertOpenSystemStoreA / CertEnumCertificatesInStore for the
#              OpenSSL trust-store import (loadWindowsRootCertificates).
#   dbghelp  – MiniDumpWriteDump for the SEH crash filter
#              (auroraCrashFilter writes %TEMP%\aurora-mail-crash-<pid>.dmp).
#   psapi    – GetModuleFileNameA for resolving the crash address to a DLL.
if(WIN32)
    target_link_libraries(aurora-mail PRIVATE crypt32 dbghelp psapi)
endif()

# Windows: GUI subsystem. macOS: .app bundle only on Apple toolchains.
set(_AURORA_MAIL_GUI_PROPS WIN32_EXECUTABLE TRUE)
if(APPLE)
    list(APPEND _AURORA_MAIL_GUI_PROPS
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
    if(QT_VERSION VERSION_LESS 6.1.0)
        list(APPEND _AURORA_MAIL_GUI_PROPS MACOSX_BUNDLE_GUI_IDENTIFIER com.aurora.mail)
    endif()
endif()
set_target_properties(aurora-mail PROPERTIES ${_AURORA_MAIL_GUI_PROPS})

# Installation
include(GNUInstallDirs)
install(TARGETS aurora-mail
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Finalize Qt executable
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(aurora-mail)
endif()

# ----------------------------------------------------------------------------
# Windows: deploy Qt 6 plugins next to the binary. Without platforms/
# qwindows.dll any Qt GUI app aborts at startup with the classic
# "no Qt platform plugin could be initialized" message box.
#
# We deliberately do NOT use windeployqt here because vcpkg's split-triplet
# layout (host tools in x64-windows, runtime DLLs in x64-windows-rel)
# confuses windeployqt's prefix-detection: it walks its own location to
# determine where Qt is installed and then fails to find runtime libraries
# that actually live in the target triplet's bin/.
#
# DLL deployment (Qt6Core.dll, qt6keychain.dll, libcrypto-3-x64.dll, …) is
# handled automatically by vcpkg's toolchain via VCPKG_APPLOCAL_DEPS=ON,
# which is the default for the Visual Studio generator. The only thing
# missing is the Qt plugin tree, which we mirror manually below.
# ----------------------------------------------------------------------------
if(WIN32 AND ${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # Qt6_DIR points at <triplet>/share/Qt6, so plugins live two levels up
    # under Qt6/plugins (vcpkg layout). Resolve once, absolute, so the path
    # is robust against in/out-of-source builds.
    get_filename_component(_AURORA_QT_PLUGINS_DIR
        "${Qt6_DIR}/../../../Qt6/plugins" ABSOLUTE)

    if(NOT EXISTS "${_AURORA_QT_PLUGINS_DIR}/platforms/qwindows.dll")
        message(WARNING
            "Qt 6 platform plugin not found at "
            "'${_AURORA_QT_PLUGINS_DIR}/platforms/qwindows.dll'. "
            "aurora-mail.exe will fail to start until you copy the Qt "
            "plugins tree next to it.")
    endif()

    # Plugins the desktop app actually needs at runtime. Each is optional:
    # if vcpkg didn't install a particular feature (e.g. no SVG support),
    # we silently skip it instead of failing the build.
    set(_AURORA_QT_REQUIRED_PLUGIN_DIRS
        platforms      # qwindows.dll – mandatory for any Qt GUI app
        styles         # native Windows 11 look-and-feel (qmodernwindowsstyle.dll)
        imageformats   # PNG/JPG/GIF/ICO attachments and inline images
        tls            # qopensslbackend.dll for IMAPS/SMTPS
        sqldrivers     # qsqlite.dll – local mail metadata cache (QSqlDatabase)
        iconengines    # qsvgicon.dll – toolbar/menu vector icons
    )

    foreach(_dir IN LISTS _AURORA_QT_REQUIRED_PLUGIN_DIRS)
        if(EXISTS "${_AURORA_QT_PLUGINS_DIR}/${_dir}")
            add_custom_command(TARGET aurora-mail POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_AURORA_QT_PLUGINS_DIR}/${_dir}"
                    "$<TARGET_FILE_DIR:aurora-mail>/${_dir}"
                COMMENT
                    "Deploying Qt6 ${_dir} plugins next to aurora-mail.exe"
                VERBATIM
            )
        endif()
    endforeach()
endif()

# ----------------------------------------------------------------------------
# Stage a default config.json next to the binary on every build. main.cpp's
# ensureConfigExists() will copy this file into <AppDataLocation>/config.json
# on first launch (per OS user), so the bundled copy is the seed for new
# installs and a reference for existing ones. The committed example file
# under Resources/ keeps "locale": "uk" and conservative logger defaults; it
# never contains credentials.
# ----------------------------------------------------------------------------
add_custom_command(TARGET aurora-mail POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/config.example.json"
        "$<TARGET_FILE_DIR:aurora-mail>/config.json"
    COMMENT "Staging default config.json next to aurora-mail.exe"
    VERBATIM
)
