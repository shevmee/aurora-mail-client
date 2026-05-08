# Aurora Mail

Cross-platform desktop mail client with asynchronous architecture and AI integration, written in C++23 and Qt 6.

## Quick start

The project provides one-command bootstrap scripts that install all build dependencies and build the application from a clean checkout.

### macOS / Linux

```bash
./scripts/bootstrap.sh           # release build (default)
./scripts/bootstrap.sh debug     # debug build with unit tests enabled
```

The script detects the host platform via `uname` and installs dependencies through Homebrew on macOS or `apt-get` on Debian/Ubuntu derivatives. It is idempotent — re-running it on an already-provisioned machine simply skips installed packages.

### Windows

```powershell
.\scripts\bootstrap.ps1                 # release build (default)
.\scripts\bootstrap.ps1 -BuildType debug
```

On Windows the script clones [microsoft/vcpkg](https://github.com/microsoft/vcpkg) into `vendor/vcpkg`, bootstraps it, and installs the project's manifest dependencies (`vcpkg.json`) for the `x64-windows` triplet. Visual Studio 2022 (Desktop development with C++) and Git must be installed beforehand.

### Manual build

If you prefer to manage dependencies yourself, use the CMake presets directly:

```bash
cmake --preset macos-release      # or linux-release / windows-release
cmake --build --preset macos-release --parallel
ctest --preset macos-debug        # only on debug presets (BUILD_TESTS=ON)
```

Available presets: `macos-debug`, `macos-release`, `linux-debug`, `linux-release`, `windows-debug`, `windows-release`. See [`CMakePresets.json`](CMakePresets.json).

## Build artifacts

| Platform | Artifact path                                  |
|----------|------------------------------------------------|
| macOS    | `build/macos-release/desktop/aurora-mail.app`  |
| Linux    | `build/linux-release/desktop/aurora-mail`      |
| Windows  | `build\windows-release\desktop\Release\aurora-mail.exe` |

## Required tooling

| Component | macOS                  | Linux (Debian/Ubuntu)              | Windows                      |
|-----------|------------------------|------------------------------------|------------------------------|
| Compiler  | Apple Clang (Xcode CLT)| g++ 13+ or Clang 16+               | MSVC 19.36+ (VS 2022)        |
| CMake     | `brew install cmake`   | `apt install cmake` (>=3.22)       | Bundled with Visual Studio   |
| Generator | Ninja                  | Ninja                              | Visual Studio 17 2022        |
| Qt        | `brew install qt`      | `apt install qt6-base-dev qt6-tools-dev` | via vcpkg            |
| Boost     | `brew install boost`   | `apt install libboost-dev`         | via vcpkg                    |
| OpenSSL   | `brew install openssl@3` | `apt install libssl-dev`         | via vcpkg                    |
| GMime     | `brew install gmime`   | `apt install libgmime-3.0-dev`     | via vcpkg                    |
| QtKeychain| `brew install qtkeychain` | `apt install qt6keychain-dev`   | via vcpkg                    |

## Developer tooling

The CMake build registers helper targets for static analysis and formatting (defined in [`cmake/AuroraDevTools.cmake`](cmake/AuroraDevTools.cmake)):

```bash
cmake --build build/macos-debug --target aurora-clang-format-check
cmake --build build/macos-debug --target aurora-clang-format
cmake --build build/macos-debug --target aurora-clang-tidy
cmake --build build/macos-debug --target aurora-clazy
```

These targets only analyse repository sources under `engine/` and `desktop/`, never SDK or third-party headers. Each target degrades gracefully if its underlying tool (`clang-format`, `clang-tidy`, `clazy`) is not installed.

## Repository layout

```
AuroraMail/
├── CMakeLists.txt           # top-level build configuration
├── CMakePresets.json        # platform × debug/release presets
├── vcpkg.json               # dependency manifest (Windows + cross-platform fallback)
├── cmake/                   # CMake helper modules (AuroraDevTools.cmake)
├── engine/                  # mail protocol library (IMAP/SMTP, MIME, GUI-neutral)
│   ├── MailClient/          # async clients (BaseClient, ImapClient, SmtpClient)
│   ├── libs/                # protocol commands, response parsing, MIME, common utilities
│   ├── Cli/                 # diagnostic CLI utilities (BaseCli → ImapCli/SmtpCli)
│   └── test_utils/          # Python integration tests driving the CLI binaries
├── desktop/                 # Qt-based GUI client
│   ├── src/Core/            # auth, AI, mail, email parser adapter, config, IO context
│   ├── src/Ui/              # main window, coordinators, custom widgets
│   ├── Resources/           # QSS theme, icons, .qrc
│   └── translations/        # Qt Linguist .ts sources (English source, Ukrainian target)
└── scripts/                 # bootstrap.sh (POSIX) and bootstrap.ps1 (Windows)
```

## Testing

Unit tests are built when `BUILD_TESTS=ON` (the default for `*-debug` presets) and run via the matching test preset:

```bash
ctest --preset macos-debug --output-on-failure
```

Integration tests against live IMAP/SMTP providers live under [`engine/test_utils/`](engine/test_utils/) and are driven by Python through the diagnostic CLI binaries. They require provider credentials in the `MAIL_USERNAME` and `MAIL_PASSWORD` environment variables and are run manually by developers, not on every build.

## License

See repository headers and per-file notices.
