@mainpage Aurora Mail — Developer Documentation

@tableofcontents

# Aurora Mail

Aurora Mail is a cross-platform desktop email client written in modern C++23,
built around a portable mail-protocol **engine** (IMAP/SMTP, MIME, OAuth) and
a Qt 6 **desktop** UI. The two halves are deliberately decoupled: the engine
has no Qt dependency and is unit-tested in isolation; the desktop is a thin,
domain-aware shell on top of it.

This documentation is generated from the source code by Doxygen. It is
intended for new contributors, code reviewers, and anyone reading the project
as part of qualification work.

# Project layout

## Engine (Qt-free, pure C++23)

| Module | Source path | Responsibility | Threading |
|---|---|---|---|
| **Stream**          | engine/libs/Common/Stream            | TLS / plain TCP transport (Boost.Asio coroutines) | single io_context thread |
| **Logging**         | engine/libs/Common/Logging           | Async logger (file + stdout, level / rotation / flush) | thread-safe |
| **MailMessage**     | engine/libs/Common/MailMessage       | Pure-data envelope types (MailAddress, MailMessage, ReceivedMailMessage) | value types |
| **MimeHandling**    | engine/libs/Common/MimeHandling      | GMime-based MIME reader / writer | thread-safe via singleton MimeContext |
| **Configs**         | engine/libs/Common/Configs           | Engine-side config structs (StartupConfig, LoggerConfig, LogMode) | value types |
| **Utils**           | engine/libs/Common/Utils             | Base64, IMAP-modified UTF-7, tag generator, command validation, ProtocolError | header-only / pure |
| **ImapResponse**    | engine/libs/ResponseHandling/Imap    | IMAP tokenizer + parser (ImapResponse) | pure parsing, no I/O |
| **SmtpResponse**    | engine/libs/ResponseHandling/Smtp    | SMTP response parser, RFC 3463 enhanced status codes | pure parsing |
| **ImapCommand**     | engine/libs/ImapCommand              | Typed builders for IMAP client commands | pure builders |
| **SmtpCommand**     | engine/libs/SmtpCommand              | Typed builders for SMTP client commands | pure builders |
| **SmtpClient**      | engine/MailClient/SmtpClient         | High-level SmtpClient (RFC 5321 state machine + STARTTLS) | runs on io_context |
| **ImapClient**      | engine/MailClient/ImapClient         | High-level ImapClient (RFC 3501 state machine + IDLE) | runs on io_context |

## Desktop (Qt 6)

| Module | Source path | Responsibility | Threading |
|---|---|---|---|
| **Auth**            | desktop/src/Core/Auth                | Account registry, OAuth 2.0 (Gmail, Outlook), QtKeychain credential store | QObject-based |
| **Mail**            | desktop/src/Core/Mail                | ImapSessionController, ImapCommandQueue, MailSessionService | bridges UI thread ↔ io_context |
| **Cache**           | desktop/src/Core/Mail/Cache          | Tiered message cache (memory LRU + AES-256-GCM SQLite) | thread-safe |
| **EmailParser**     | desktop/src/Core/Email/EmailParser   | Qt-friendly wrapper around the engine's MIME reader | pure |
| **TextSanitizer**   | desktop/src/Core/Utils/TextSanitizer | Plain-text / HTML sanitization for safe QTextBrowser rendering | pure |
| **AppConfig**       | desktop/src/Core/Config/AppConfig    | JSON loader for `<AppDataLocation>/config.json` (Qt-side) | pure |
| **AI**              | desktop/src/Core/AI                  | REST client for the AI assistant (compose / summarise) | QNetworkAccessManager |
| **Ui**              | desktop/src/Ui                      | MainWindow, dialogs, widgets | Qt main thread only |

# Architecture at a glance

@htmlonly
<pre class="mermaid">
flowchart TB
    classDef ui      fill:#7c5cff,stroke:#4a32c5,color:#fff,stroke-width:1px;
    classDef core    fill:#3aa0ff,stroke:#1d6bbd,color:#fff,stroke-width:1px;
    classDef engine  fill:#1ec6a6,stroke:#0e7e69,color:#fff,stroke-width:1px;
    classDef io      fill:#f59e0b,stroke:#a76700,color:#fff,stroke-width:1px;

    subgraph DESKTOP["Desktop &nbsp;·&nbsp; Qt 6 GUI"]
        direction TB
        UI["MainWindow / Coordinators / Widgets"]:::ui

        subgraph CORE["AuroraMailCore (Qt, no GUI types)"]
            direction LR
            Auth["Auth · OAuth · Keychain"]:::core
            Sess["MailSessionService"]:::core
            Cache["TieredMessageCache (memory + AES-GCM SQLite)"]:::core
            Cfg["AppConfig"]:::core
            EmailParser["EmailParser"]:::core
            AI["AIService"]:::core
        end

        UI --> CORE
    end

    subgraph ENGINE["AuroraMailEngine &nbsp;·&nbsp; headless, Qt-free"]
        direction TB
        Imap["ImapClient (RFC 3501 + IDLE)"]:::engine
        Smtp["SmtpClient (RFC 5321 + STARTTLS)"]:::engine

        subgraph PARSE["Parsers / Builders"]
            direction LR
            ImapCmd["ImapCommand"]:::engine
            SmtpCmd["SmtpCommand"]:::engine
            ImapResp["ImapResponse"]:::engine
            SmtpResp["SmtpResponse"]:::engine
        end

        subgraph LOWER["Common building blocks"]
            direction LR
            Stream["Stream (TLS / TCP)"]:::engine
            Mime["MimeHandling (GMime)"]:::engine
            Logger["Logger"]:::engine
            Utils["Utils (Base64, UTF-7, ProtocolError, ...)"]:::engine
        end

        Imap --> PARSE
        Smtp --> PARSE
        Imap --> LOWER
        Smtp --> LOWER
    end

    IO["boost::asio::io_context · 1 worker thread"]:::io

    CORE -- "function calls (no Qt types crossing)" --> ENGINE
    ENGINE -.coroutines.- IO
</pre>
@endhtmlonly

# Key design choices

- **No coupling from engine to Qt.** Anything that would require Qt lives in
  `desktop/src/Core/...`. The engine compiles and tests as a standalone CLI
  tool (`aurora-mail-cli`), and the unit tests under `engine/libs/.../test/`
  run without a Qt event loop.

- **Coroutines + a single io_context.** Both protocol clients are written as
  `boost::asio::awaitable<>` coroutines. The desktop owns one io_context
  worker thread (`desktop/src/Core/IoContext`) and the UI thread talks to it
  via signal/slot bridges (`MailSessionSignals`, `ImapCommandQueue`).

- **Strongly-typed errors via `std::expected`.** The engine returns
  `std::expected<T, ProtocolError>` (and `MimeParseError` for MIME) instead of
  exceptions. This makes the failure surface explicit and unit-testable.

- **Tiered message cache.** Hot paths hit an in-memory LRU
  (`MemoryMessageCache`); cold reads fall through to a per-account SQLite tier
  whose blobs are AES-256-GCM-encrypted under a keychain-resident master key
  (`AesGcmCipher`, `CacheKeyMaterial`).

- **Test pyramid.** The repository ships ~480 GoogleTest cases covering every
  pure-logic module. See @ref testing below.

# Testing                                                  {#testing}

Build and run the full test suite:

```bash
cmake --preset macos-debug
cmake --build build/macos-debug
ctest --test-dir build/macos-debug --output-on-failure
```

Engine tests live next to each library (`engine/libs/.../test/Tests*.cpp`).
Desktop tests live in `desktop/test/` and are aggregated into the
`DesktopCoreTests` GoogleTest binary, which builds its own `QCoreApplication`
in `DesktopTestMain.cpp`.

# Reading order suggestions

For a code-review pass, the following entry points give the densest signal.
Use the **search box** at the top of the page to jump to any of them — the
search index covers classes, namespaces, free functions and source files.

1. `aurora::mail::imap::ImapClient` (engine) — top of the IMAP stack.
2. `aurora::mail::smtp::SmtpClient` (engine) — top of the SMTP stack.
3. `aurora::mail::common::mime::reader::parseMessage` (engine) — MIME ingestion.
4. `aurora::mail::app::cache::TieredMessageCache` (desktop) — local persistence.
5. `aurora::mail::app::email::EmailParser` (desktop) — Qt ↔ engine adapter.

# Building the docs

```bash
# Engine documentation
doxygen engine/Doxyfile
open engine/docs/html/index.html        # macOS
xdg-open engine/docs/html/index.html    # Linux

# Desktop documentation
doxygen desktop/Doxyfile
open desktop/docs/html/index.html
```

The CMake project also exposes an `aurora-docs` target that builds both:

```bash
cmake --build build/macos-debug --target aurora-docs
```
