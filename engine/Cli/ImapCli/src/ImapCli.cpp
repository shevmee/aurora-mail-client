#include <ImapCli.hpp>
#include <MimeReader.hpp>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace aurora::mail::cli
{

  // Alias for convenience
  namespace mime = aurora::mail::common::mime::reader;

  std::optional<CachedMessage> ImapCli::parseFetchResponse(const std::string& line)
  {
    // Skip non-FETCH lines
    if (line.find("* ") != 0 || line.find("FETCH") == std::string::npos)
    {
      return std::nullopt;
    }

    CachedMessage msg;

    // Parse UID (required)
    std::regex uid_regex(R"(UID\s+(\d+))", std::regex::icase);
    std::smatch uid_match;
    if (!std::regex_search(line, uid_match, uid_regex))
    {
      return std::nullopt;
    }
    msg.uid = std::stoul(uid_match[1].str());

    // Parse FLAGS
    std::regex flags_regex(R"(FLAGS\s*\(([^)]*)\))", std::regex::icase);
    std::smatch flags_match;
    if (std::regex_search(line, flags_match, flags_regex))
    {
      msg.flags = flags_match[1].str();
    }

    // Parse RFC822.SIZE
    std::regex size_regex(R"(RFC822\.SIZE\s+(\d+))", std::regex::icase);
    std::smatch size_match;
    if (std::regex_search(line, size_match, size_regex))
    {
      msg.size = std::stoul(size_match[1].str());
    }

    // Parse MODSEQ (CONDSTORE extension)
    std::regex modseq_regex(R"(MODSEQ\s*\((\d+)\))", std::regex::icase);
    std::smatch modseq_match;
    if (std::regex_search(line, modseq_match, modseq_regex))
    {
      msg.modseq = std::stoull(modseq_match[1].str());
    }

    // Parse ENVELOPE - find ENVELOPE (...) and extract the parenthesized content
    size_t env_pos = line.find("ENVELOPE");
    if (env_pos == std::string::npos)
    {
      env_pos = line.find("envelope");
    }
    if (env_pos != std::string::npos)
    {
      size_t paren_start = line.find('(', env_pos);
      if (paren_start != std::string::npos)
      {
        size_t paren_end = findMatchingParen(line, paren_start);
        if (paren_end != std::string::npos)
        {
          std::string envelope = line.substr(paren_start, paren_end - paren_start + 1);
          parseEnvelope(envelope, msg);
        }
      }
    }

    return msg;
  }

  size_t ImapCli::parseFetchResponsesAndUpdateCache(const std::string& response)
  {
    if (current_mailbox.empty())
      return 0;

    std::vector<CachedMessage> messages;
    uint64_t max_modseq = 0;

    std::istringstream iss(response);
    std::string line;
    while (std::getline(iss, line))
    {
      auto msg_opt = parseFetchResponse(line);
      if (msg_opt)
      {
        messages.push_back(*msg_opt);
        if (msg_opt->modseq > max_modseq)
        {
          max_modseq = msg_opt->modseq;
        }
      }
    }

    if (!messages.empty())
    {
      // Ensure cache has proper mailbox state before adding messages
      // (handles case where cache was cleared but mailbox is still selected)
      if (!cache.getUidValidity(current_mailbox).has_value() && current_uidvalidity > 0)
      {
        cache.setMailboxState(current_mailbox, current_uidvalidity, current_highestmodseq);
      }

      cache.addMessages(current_mailbox, messages);
      if (max_modseq > 0)
      {
        cache.updateHighestModSeq(current_mailbox, max_modseq);
      }
    }

    return messages.size();
  }

  std::vector<uint32_t> ImapCli::parseUidSet(const std::string& uid_set)
  {
    std::vector<uint32_t> result;

    std::istringstream iss(uid_set);
    std::string token;
    while (std::getline(iss, token, ','))
    {
      size_t colon = token.find(':');
      if (colon != std::string::npos)
      {
        // Range: "start:end"
        uint32_t start = std::stoul(token.substr(0, colon));
        uint32_t end = std::stoul(token.substr(colon + 1));
        for (uint32_t i = start; i <= end; ++i)
        {
          result.push_back(i);
        }
      }
      else
      {
        // Single UID
        result.push_back(std::stoul(token));
      }
    }

    return result;
  }

  std::vector<uint32_t> ImapCli::parseVanishedResponse(const std::string& data)
  {
    // VANISHED response: "* VANISHED 1:5,10,15:20" or "* VANISHED (EARLIER) 1:5"
    std::string uid_part = data;

    // Remove "(EARLIER)" if present
    size_t paren = uid_part.find("(EARLIER)");
    if (paren != std::string::npos)
    {
      uid_part = uid_part.substr(paren + 9);
    }

    // Trim whitespace
    while (!uid_part.empty() && std::isspace(uid_part.front()))
    {
      uid_part.erase(uid_part.begin());
    }

    return parseUidSet(uid_part);
  }

  std::pair<std::string, size_t> ImapCli::extractQuotedOrNil(const std::string& s, size_t pos)
  {
    // Skip whitespace
    while (pos < s.size() && std::isspace(s[pos]))
      pos++;

    if (pos >= s.size())
      return { std::string(), pos };

    // Check for NIL
    if (pos + 3 <= s.size() && (s.substr(pos, 3) == "NIL" || s.substr(pos, 3) == "nil"))
    {
      return { std::string(), pos + 3 };
    }

    // Check for quoted string
    if (s[pos] == '"')
    {
      size_t start = pos + 1;
      size_t end = start;
      while (end < s.size())
      {
        if (s[end] == '\\' && end + 1 < s.size())
        {
          end += 2;  // Skip escaped character
        }
        else if (s[end] == '"')
        {
          break;
        }
        else
        {
          end++;
        }
      }
      std::string result = s.substr(start, end - start);
      // Unescape backslash-escaped characters
      size_t i = 0;
      while ((i = result.find('\\', i)) != std::string::npos && i + 1 < result.size())
      {
        result.erase(i, 1);
        i++;
      }
      return { result, end + 1 };
    }

    return { std::string(), pos };
  }

  size_t ImapCli::findMatchingParen(const std::string& s, size_t open_pos)
  {
    if (open_pos >= s.size() || s[open_pos] != '(')
      return std::string::npos;

    unsigned int depth = 1;
    bool in_quote = false;

    for (size_t i = open_pos + 1; i < s.size(); i++)
    {
      char c = s[i];
      if (c == '\\' && in_quote && i + 1 < s.size())
      {
        i++;  // Skip escaped character
      }
      else if (c == '"')
      {
        in_quote = !in_quote;
      }
      else if (!in_quote)
      {
        if (c == '(')
          depth++;
        else if (c == ')')
        {
          depth--;
          if (depth == 0)
            return i;
        }
      }
    }
    return std::string::npos;
  }

  std::string ImapCli::decodeRfc2047(const std::string& encoded)
  {
    // Delegate to MimeParser's decodeHeaderValue (uses GMime for robust RFC 2047
    // decoding)
    return mime::decodeHeaderValue(encoded);
  }

  std::string ImapCli::parseAddressList(const std::string& addr_list)
  {
    // Address list format: ((personal-name NIL mailbox-name host-name) ...)
    // or NIL for empty
    if (addr_list.empty() || addr_list == "NIL" || addr_list == "nil")
    {
      return std::string();
    }

    std::vector<std::string> addresses;
    size_t pos = 0;

    // Skip outer parentheses if present
    while (pos < addr_list.size() && std::isspace(addr_list[pos]))
      pos++;
    if (pos < addr_list.size() && addr_list[pos] == '(')
      pos++;

    while (pos < addr_list.size())
    {
      // Skip whitespace
      while (pos < addr_list.size() && std::isspace(addr_list[pos]))
        pos++;

      if (pos >= addr_list.size() || addr_list[pos] == ')')
        break;

      // Find start of address tuple
      if (addr_list[pos] != '(')
      {
        pos++;
        continue;
      }

      size_t tuple_end = findMatchingParen(addr_list, pos);
      if (tuple_end == std::string::npos)
        break;

      std::string tuple = addr_list.substr(pos + 1, tuple_end - pos - 1);
      pos = tuple_end + 1;

      // Parse tuple: (personal-name NIL mailbox-name host-name)
      size_t tpos = 0;
      auto [personal, p1] = extractQuotedOrNil(tuple, tpos);
      auto [route, p2] = extractQuotedOrNil(tuple, p1);  // Usually NIL (source route, obsolete)
      auto [mailbox, p3] = extractQuotedOrNil(tuple, p2);
      auto [host, p4] = extractQuotedOrNil(tuple, p3);

      std::string addr;
      if (!personal.empty())
      {
        std::string decoded_personal = decodeRfc2047(personal);
        addr = decoded_personal + " ";
      }
      if (!mailbox.empty())
      {
        addr += "<" + mailbox;
        if (!host.empty())
        {
          addr += "@" + host;
        }
        addr += ">";
      }

      if (!addr.empty())
      {
        addresses.push_back(addr);
      }
    }

    // Join addresses with comma
    std::string result;
    for (size_t i = 0; i < addresses.size(); i++)
    {
      if (i > 0)
        result += ", ";
      result += addresses[i];
    }
    return result;
  }

  void ImapCli::parseEnvelope(const std::string& envelope_str, CachedMessage& msg)
  {
    // ENVELOPE format (RFC 3501):
    // ("date" "subject" ((from)) ((sender)) ((reply-to)) ((to)) ((cc)) ((bcc))
    // "in-reply-to" "message-id")

    if (envelope_str.empty())
      return;

    // Helper to parse an address list field
    auto parseAddressField = [this, &envelope_str](size_t& pos) -> std::string
    {
      while (pos < envelope_str.size() && std::isspace(envelope_str[pos]))
        pos++;
      if (pos < envelope_str.size() && envelope_str[pos] == '(')
      {
        size_t end = findMatchingParen(envelope_str, pos);
        if (end != std::string::npos)
        {
          std::string addr_list = envelope_str.substr(pos, end - pos + 1);
          pos = end + 1;
          return parseAddressList(addr_list);
        }
      }
      else if (
          pos + 3 <= envelope_str.size() && (envelope_str.substr(pos, 3) == "NIL" || envelope_str.substr(pos, 3) == "nil"))
      {
        pos += 3;
      }
      return std::string();
    };

    size_t pos = 0;

    // Skip leading whitespace and opening paren
    while (pos < envelope_str.size() && std::isspace(envelope_str[pos]))
      pos++;
    if (pos < envelope_str.size() && envelope_str[pos] == '(')
      pos++;

    // 1. Date
    auto [date_str, p1] = extractQuotedOrNil(envelope_str, pos);
    msg.date = date_str;
    pos = p1;

    // 2. Subject
    auto [subject_str, p2] = extractQuotedOrNil(envelope_str, pos);
    msg.subject = decodeRfc2047(subject_str);
    pos = p2;

    // 3. From (address list)
    msg.from = parseAddressField(pos);

    // 4. Sender (usually same as from, skip)
    parseAddressField(pos);

    // 5. Reply-To (skip for now)
    parseAddressField(pos);

    // 6. To (address list)
    msg.to = parseAddressField(pos);

    // 7. Cc (address list)
    msg.cc = parseAddressField(pos);

    // 8. Bcc (address list) - usually empty for received messages
    msg.bcc = parseAddressField(pos);

    // 9. In-Reply-To
    auto [in_reply_to, p3] = extractQuotedOrNil(envelope_str, pos);
    if (!in_reply_to.empty())
    {
      msg.in_reply_to = in_reply_to;
    }
    pos = p3;

    // 10. Message-ID
    auto [message_id, p4] = extractQuotedOrNil(envelope_str, pos);
    msg.message_id = message_id;
    (void)p4;  // Suppress unused warning

    msg.has_envelope = true;
  }

  ImapCli::ImapCli(asio::io_context& io_ctx, ssl::context& ssl_ctx, const aurora::mail::common::config::StartupConfig& cfg)
      : BaseCli(io_ctx, ssl_ctx, cfg),
        qresync_enabled(false)
  {
    imap_client = std::make_unique<aurora::mail::imap::ImapClient>(io_context, ssl_context, config.timeout_seconds);

    // Register callback for unsolicited notifications (EXISTS, EXPUNGE, VANISHED,
    // FETCH)
    imap_client->setUnsolicitedCallback([this](const aurora::mail::imap::response::UntaggedResponse& resp)
                                        { handleUnsolicitedResponse(resp); });

    initializeCommands();
  }

  std::string ImapCli::getPrompt() const
  {
    std::string prompt = "imap";
    if (!current_mailbox.empty())
    {
      prompt += "[" + current_mailbox + "]";
    }
    prompt += "> ";
    return prompt;
  }

  std::string ImapCli::getWelcomeMessage() const
  {
    return "=== IMAP CLI Client ===\n";
  }

  void ImapCli::initializeCommands()
  {
    // Connection
    registerCommand(
        "connect", [this](const auto& args) { handleConnect(args); }, "connect [host] [port] - Connect to IMAP server");
    registerCommand("login", [this](const auto& args) { handleLogin(args); }, "login <user> <pass> - Authenticate");
    registerCommand("logout", [this](const auto&) { handleLogout(); }, "logout - Logout");

    // Mailbox operations
    registerCommand("list", [this](const auto& args) { handleList(args); }, "list [pattern] - List mailboxes");
    registerCommand("lsub", [this](const auto& args) { handleLsub(args); }, "lsub [pattern] - List subscribed mailboxes");
    registerCommand(
        "select", [this](const auto& args) { handleSelect(args); }, "select <mailbox> - Select mailbox (with CONDSTORE)");
    registerCommand(
        "examine", [this](const auto& args) { handleExamine(args); }, "examine <mailbox> - Examine mailbox (read-only)");
    registerCommand("create", [this](const auto& args) { handleCreate(args); }, "create <mailbox> - Create a new mailbox");
    registerCommand("delete", [this](const auto& args) { handleDelete(args); }, "delete <mailbox> - Delete a mailbox");
    registerCommand("rename", [this](const auto& args) { handleRename(args); }, "rename <old> <new> - Rename a mailbox");
    registerCommand(
        "subscribe", [this](const auto& args) { handleSubscribe(args); }, "subscribe <mailbox> - Subscribe to a mailbox");
    registerCommand(
        "unsubscribe",
        [this](const auto& args) { handleUnsubscribe(args); },
        "unsubscribe <mailbox> - Unsubscribe from a mailbox");
    registerCommand("close", [this](const auto&) { handleClose(); }, "close - Close mailbox (expunge deleted messages)");
    registerCommand("expunge-all", [this](const auto&) { handleExpunge(); }, "expunge-all - Expunge all deleted messages");
    registerCommand("status", [this](const auto& args) { handleStatus(args); }, "status <mailbox> - Get mailbox status");

    // UID-based message operations (primary)
    registerCommand(
        "search",
        [this](const auto& args) { handleUidSearch(args); },
        "search <criteria> - UID SEARCH (e.g., 'ALL', 'UNSEEN')");
    registerCommand(
        "fetch", [this](const auto& args) { handleUidFetch(args); }, "fetch <uid-set> [items] - UID FETCH messages");
    registerCommand(
        "store", [this](const auto& args) { handleUidStore(args); }, "store <uid-set> <flags> - UID STORE flags");
    registerCommand(
        "copy", [this](const auto& args) { handleUidCopy(args); }, "copy <uid-set> <mailbox> - UID COPY messages");
    registerCommand(
        "move", [this](const auto& args) { handleUidMove(args); }, "move <uid-set> <mailbox> - UID MOVE messages");
    registerCommand(
        "expunge", [this](const auto& args) { handleUidExpunge(args); }, "expunge <uid-set> - UID EXPUNGE deleted messages");
    registerCommand(
        "read",
        [this](const auto& args) { handleRead(args); },
        "read <uid> - Read message body (fetches and caches if needed)");

    // Sync operations
    registerCommand("sync", [this](const auto& args) { handleSync(args); }, "sync - Sync mailbox using CONDSTORE/QRESYNC");
    registerCommand(
        "headers",
        [this](const auto& args) { handleFetchHeaders(args); },
        "headers [uid-set] - Fetch message headers for list view");
    registerCommand(
        "append",
        [this](const auto& args) { handleAppend(args); },
        "append <mailbox> <file> [flags] - Upload message from file");

    // IDLE
    registerCommand(
        "idle", [this](const auto& args) { handleIdle(args); }, "idle [seconds] - Enter IDLE mode for notifications");

    // Cache operations
    registerCommand("cache", [this](const auto& args) { handleCacheInfo(args); }, "cache - Show cache statistics");
    registerCommand("clear-cache", [this](const auto&) { handleClearCache(); }, "clear-cache - Clear message cache");

    // Utility
    registerCommand("caps", [this](const auto&) { handleCapability(); }, "caps - Show server capabilities");
    registerCommand("noop", [this](const auto&) { handleNoop(); }, "noop - Send NOOP (check for updates)");

    // Built-in commands (from base class)
    registerBuiltinCommands();
  }

  void ImapCli::handleUnsolicitedResponse(const aurora::mail::imap::response::UntaggedResponse& resp)
  {
    // Handle server notifications in real-time
    if (resp.command == "EXISTS")
    {
      std::cout << "\n[Server] New mail! Mailbox now has " << resp.data << " messages\n";
      std::cout << getPrompt() << std::flush;
      // Note: Should fetch new messages (UIDNEXT:*) to update cache
    }
    else if (resp.command == "EXPUNGE")
    {
      std::cout << "\n[Server] Message expunged: sequence " << resp.data << "\n";
      std::cout << getPrompt() << std::flush;
      // Note: Sequence-based expunge is tricky - would need seq->UID mapping
      // QRESYNC uses VANISHED instead which gives UIDs directly
    }
    else if (resp.command == "VANISHED")
    {
      // QRESYNC extension - gives UIDs directly
      auto vanished_uids = parseVanishedResponse(std::string(resp.data));
      if (!vanished_uids.empty() && !current_mailbox.empty())
      {
        cache.removeMessages(current_mailbox, vanished_uids);
        std::cout << "\n[Server] VANISHED: " << vanished_uids.size() << " message(s) removed from cache\n";
        std::cout << getPrompt() << std::flush;
      }
    }
    else if (resp.command == "FETCH")
    {
      // Parse the FETCH response and update cache
      std::string fetch_line = "* " + std::string(resp.data);
      auto msg_opt = parseFetchResponse(fetch_line);
      if (msg_opt && !current_mailbox.empty())
      {
        cache.addMessage(current_mailbox, *msg_opt);
        std::cout << "\n[Server] Message UID " << msg_opt->uid << " updated: " << msg_opt->flags << "\n";
        std::cout << getPrompt() << std::flush;
      }
    }
  }

  // --- Connection ---

  void ImapCli::handleConnect(const std::vector<std::string>& args)
  {
    std::string host = args.size() > 1 ? args[1] : config.imap.host;
    uint16_t port = args.size() > 2 ? static_cast<uint16_t>(std::stoul(args[2])) : config.imap.getPort();

    std::cout << "Connecting to " << host << ":" << port << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, host, port, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncConnect(host, port);
          if (result.has_value())
          {
            std::cout << "Connected successfully\n";
          }
          else
          {
            std::cout << "Connection failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleLogin(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: login <username> <password>\n";
      return;
    }

    const std::string& username = args[1];
    // Join all remaining args as password (handles passwords with spaces)
    std::string password;
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (i > 2)
        password += " ";
      password += args[i];
    }

    std::cout << "Logging in as " << username << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, username, password, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncLogin(username, password);
          if (result.has_value())
          {
            std::cout << "Login successful\n";

            // Get capabilities after login
            auto caps = co_await imap_client->asyncCapability();
            if (caps.has_value())
            {
              bool has_condstore = caps->hasCondstore();
              bool has_qresync = caps->hasQresync();
              bool has_idle = caps->hasIdle();
              bool has_move = caps->hasMove();
              bool has_uidplus = caps->hasUidplus();

              std::cout << "Sync extensions: ";
              if (has_idle)
                std::cout << "IDLE ";
              if (has_condstore)
                std::cout << "CONDSTORE ";
              if (has_qresync)
                std::cout << "QRESYNC ";
              if (has_move)
                std::cout << "MOVE ";
              if (has_uidplus)
                std::cout << "UIDPLUS ";
              std::cout << "\n";

              // Enable QRESYNC if available (also enables CONDSTORE)
              if (has_qresync)
              {
                auto enable_result = co_await imap_client->asyncEnableQresync();
                if (enable_result.has_value())
                {
                  qresync_enabled = true;
                  std::cout << "QRESYNC enabled for efficient sync\n";
                }
              }
            }
          }
          else
          {
            std::cout << "Login failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleLogout()
  {
    std::cout << "Logging out...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncLogout();
          if (result.has_value())
          {
            std::cout << "Logged out successfully\n";
            current_mailbox.clear();
            qresync_enabled = false;  // Reset for next session
            // Note: Cache is preserved for next login (QRESYNC can use it)
          }
          else
          {
            std::cout << "Logout failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  // --- Mailbox Operations ---

  void ImapCli::handleList(const std::vector<std::string>& args)
  {
    std::string pattern = args.size() > 1 ? args[1] : "*";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, pattern, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncListMailboxes("", pattern);
          if (result.has_value())
          {
            std::cout << "Mailboxes:\n";
            for (const auto& mb : result.value())
            {
              std::string decoded = mb.getDecodedName();
              std::cout << "  " << decoded;
              if (mb.hasAttribute("\\Noselect"))
              {
                std::cout << " (not selectable)";
              }
              std::cout << "\n";
            }
          }
          else
          {
            std::cout << "LIST failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleSelect(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: select <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];

    // Check if we can use QRESYNC for instant sync
    bool use_qresync = qresync_enabled && cache.isValidForQresync(mailbox);

    if (use_qresync)
    {
      auto cached_uidvalidity = cache.getUidValidity(mailbox);
      auto cached_modseq = cache.getHighestModSeq(mailbox);
      std::string known_uids = cache.getKnownUidRanges(mailbox);

      std::cout << "Selecting " << mailbox << " with QRESYNC (instant sync)...\n";
      std::cout << "  Using cached UIDVALIDITY=" << *cached_uidvalidity << ", MODSEQ=" << *cached_modseq << "\n";

      std::promise<void> done;
      auto fut = done.get_future();

      asio::co_spawn(
          io_context,
          [this, mailbox, cached_uidvalidity, cached_modseq, known_uids, &done]() -> asio::awaitable<void>
          {
            auto result = co_await imap_client->asyncSelectQresync(mailbox, *cached_uidvalidity, *cached_modseq, known_uids);

            if (result.has_value())
            {
              auto& resp = result.value();
              current_mailbox = mailbox;
              current_uidvalidity = resp.uidvalidity;
              current_highestmodseq = resp.highestmodseq;

              // Handle expunged messages from QRESYNC
              if (!resp.expunged_uids.empty())
              {
                cache.removeMessages(mailbox, resp.expunged_uids);
                std::cout << "  Removed " << resp.expunged_uids.size() << " expunged message(s) from cache\n";
              }

              // Update cache state with new HIGHESTMODSEQ
              cache.setMailboxState(mailbox, resp.uidvalidity, resp.highestmodseq);
              cache.setUidNext(mailbox, resp.uidnext);

              std::cout << "Selected: " << mailbox << " (QRESYNC)\n";
              std::cout << "  Messages: " << resp.exists << "\n";
              std::cout << "  UIDVALIDITY: " << resp.uidvalidity << "\n";
              std::cout << "  UIDNEXT: " << resp.uidnext << "\n";
              std::cout << "  HIGHESTMODSEQ: " << resp.highestmodseq << "\n";
              std::cout << "  Cached: " << cache.messageCount(mailbox) << " messages\n";
              // Note: Changed flags are delivered via unsolicited FETCH responses
            }
            else
            {
              std::cout << "QRESYNC SELECT failed: " << result.error().toString() << "\n";
              std::cout << "Falling back to CONDSTORE SELECT...\n";
              // Could fall back to regular CONDSTORE select here
            }
            done.set_value();
          },
          asio::detached);

      waitForOperation(fut);
    }
    else
    {
      // Regular CONDSTORE SELECT
      std::cout << "Selecting " << mailbox << " with CONDSTORE...\n";

      std::promise<void> done;
      auto fut = done.get_future();

      asio::co_spawn(
          io_context,
          [this, mailbox, &done]() -> asio::awaitable<void>
          {
            auto result = co_await imap_client->asyncSelectCondstore(mailbox);
            if (result.has_value())
            {
              auto& resp = result.value();
              current_mailbox = mailbox;
              current_uidvalidity = resp.uidvalidity;
              current_highestmodseq = resp.highestmodseq;

              // Update cache state
              cache.setMailboxState(mailbox, resp.uidvalidity, resp.highestmodseq);
              cache.setUidNext(mailbox, resp.uidnext);

              std::cout << "Selected: " << mailbox << "\n";
              std::cout << "  Messages: " << resp.exists << "\n";
              std::cout << "  Recent: " << resp.recent << "\n";
              std::cout << "  UIDVALIDITY: " << resp.uidvalidity << "\n";
              std::cout << "  UIDNEXT: " << resp.uidnext << "\n";
              std::cout << "  HIGHESTMODSEQ: " << resp.highestmodseq << "\n";

              size_t cached = cache.messageCount(mailbox);
              if (cached > 0)
              {
                std::cout << "  Cached: " << cached << " messages (use 'sync' to update)\n";
              }
            }
            else
            {
              std::cout << "SELECT failed: " << result.error().toString() << "\n";
            }
            done.set_value();
          },
          asio::detached);

      waitForOperation(fut);
    }
  }

  void ImapCli::handleStatus(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: status <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncStatus(mailbox, "MESSAGES UIDNEXT UIDVALIDITY UNSEEN");
          if (result.has_value())
          {
            std::cout << "Status of " << mailbox << ":\n";
            std::cout << result.value() << "\n";
          }
          else
          {
            std::cout << "STATUS failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleCreate(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: create <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];
    std::cout << "Creating mailbox: " << mailbox << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncCreateMailbox(mailbox);
          if (result.has_value())
          {
            std::cout << "CREATE completed OK\n";
          }
          else
          {
            std::cout << "CREATE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleDelete(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: delete <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];
    std::cout << "Deleting mailbox: " << mailbox << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncDeleteMailbox(mailbox);
          if (result.has_value())
          {
            std::cout << "DELETE completed OK\n";
          }
          else
          {
            std::cout << "DELETE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleExamine(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: examine <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];
    std::cout << "Examining " << mailbox << " (read-only)...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncExamineCondstore(mailbox);
          if (result.has_value())
          {
            auto& resp = result.value();
            current_mailbox = mailbox;
            current_uidvalidity = resp.uidvalidity;
            current_highestmodseq = resp.highestmodseq;

            std::cout << "Examined: " << mailbox << " (READ-ONLY)\n";
            std::cout << "  Messages: " << resp.exists << "\n";
            std::cout << "  UIDVALIDITY: " << resp.uidvalidity << "\n";
            std::cout << "  UIDNEXT: " << resp.uidnext << "\n";
            std::cout << "  HIGHESTMODSEQ: " << resp.highestmodseq << "\n";
          }
          else
          {
            std::cout << "EXAMINE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleRename(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: rename <old-name> <new-name>\n";
      return;
    }

    const std::string& old_name = args[1];
    const std::string& new_name = args[2];
    std::cout << "Renaming mailbox: " << old_name << " -> " << new_name << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, old_name, new_name, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncRenameMailbox(old_name, new_name);
          if (result.has_value())
          {
            std::cout << "RENAME completed OK\n";
          }
          else
          {
            std::cout << "RENAME failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleSubscribe(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: subscribe <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];
    std::cout << "Subscribing to: " << mailbox << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncSubscribe(mailbox);
          if (result.has_value())
          {
            std::cout << "SUBSCRIBE completed OK\n";
          }
          else
          {
            std::cout << "SUBSCRIBE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUnsubscribe(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: unsubscribe <mailbox>\n";
      return;
    }

    const std::string& mailbox = args[1];
    std::cout << "Unsubscribing from: " << mailbox << "\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncUnsubscribe(mailbox);
          if (result.has_value())
          {
            std::cout << "UNSUBSCRIBE completed OK\n";
          }
          else
          {
            std::cout << "UNSUBSCRIBE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleLsub(const std::vector<std::string>& args)
  {
    std::string pattern = args.size() > 1 ? args[1] : "*";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, pattern, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncListSubscribed("", pattern);
          if (result.has_value())
          {
            std::cout << "Subscribed mailboxes:\n";
            for (const auto& mb : result.value())
            {
              std::string decoded = mb.getDecodedName();
              std::cout << "  " << decoded << "\n";
            }
            if (result.value().empty())
            {
              std::cout << "  (none)\n";
            }
          }
          else
          {
            std::cout << "LSUB failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleClose()
  {
    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected.\n";
      return;
    }

    std::cout << "Closing mailbox (expunging deleted messages)...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncClose();
          if (result.has_value())
          {
            std::cout << "CLOSE completed OK\n";
            current_mailbox.clear();
            current_uidvalidity = 0;
            current_highestmodseq = 0;
          }
          else
          {
            std::cout << "CLOSE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleExpunge()
  {
    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected.\n";
      return;
    }

    std::cout << "Expunging all deleted messages...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncExpunge();
          if (result.has_value())
          {
            std::cout << "EXPUNGE completed OK\n";
          }
          else
          {
            std::cout << "EXPUNGE failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleAppend(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: append <mailbox> <file> [flags]\n";
      std::cout << "Example: append INBOX message.eml \"(\\\\Seen)\"\n";
      return;
    }

    const std::string& mailbox = args[1];
    const std::string& filepath = args[2];
    std::string flags = args.size() > 3 ? args[3] : "";

    // Read file content
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
      std::cout << "Cannot open file: " << filepath << "\n";
      return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string message = buffer.str();

    if (message.empty())
    {
      std::cout << "File is empty: " << filepath << "\n";
      return;
    }

    std::cout << "Uploading message to " << mailbox << " (" << message.size() << " bytes)...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, mailbox, message, flags, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncAppend(mailbox, message, flags);
          if (result.has_value())
          {
            std::cout << "APPEND completed OK\n";
          }
          else
          {
            std::cout << "APPEND failed: " << result.error().toString() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  // --- UID-based Message Operations ---

  void ImapCli::handleUidSearch(const std::vector<std::string>& args)
  {
    std::string criteria = "ALL";
    if (args.size() > 1)
    {
      criteria.clear();
      for (size_t i = 1; i < args.size(); ++i)
      {
        if (i > 1)
          criteria += " ";
        criteria += args[i];
      }
    }

    std::cout << "UID SEARCH " << criteria << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, criteria, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncUidSearchMail(criteria);
          if (result.has_value())
          {
            std::cout << "UIDs: " << result.value() << "\n";
          }
          else
          {
            std::cout << "SEARCH failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUidFetch(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: fetch <uid-set> [data-items]\n";
      std::cout << "Examples:\n";
      std::cout << "  fetch 1:10 FLAGS\n";
      std::cout << "  fetch 123 BODY[]\n";
      std::cout << "  fetch 1:* ENVELOPE\n";
      return;
    }

    const std::string& uid_set = args[1];
    std::string items = args.size() > 2 ? args[2] : "FLAGS";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, items, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncUidFetchMail(uid_set, items);
          if (result.has_value())
          {
            // Show first 500 chars of response
            const std::string& resp = result.value();
            if (resp.length() > 500)
            {
              std::cout << resp.substr(0, 500) << "\n... (" << resp.length() - 500 << " more bytes)\n";
            }
            else
            {
              std::cout << resp << "\n";
            }
          }
          else
          {
            std::cout << "FETCH failed\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUidStore(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: store <uid-set> <flags-action>\n";
      std::cout << "Examples:\n";
      std::cout << "  store 123 +FLAGS (\\Seen)\n";
      std::cout << "  store 1:10 -FLAGS (\\Flagged)\n";
      std::cout << "  store 123 FLAGS (\\Seen \\Answered)\n";
      return;
    }

    const std::string& uid_set = args[1];
    std::string flags;
    for (size_t i = 2; i < args.size(); ++i)
    {
      if (i > 2)
        flags += " ";
      flags += args[i];
    }

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, flags, &done]() -> asio::awaitable<void>
        {
          try
          {
            auto result = co_await imap_client->asyncUidStoreMail(uid_set, flags);
            if (result.has_value())
            {
              // Parse the response to get updated flags and MODSEQ
              size_t updated = parseFetchResponsesAndUpdateCache(result.value());
              if (updated > 0)
              {
                std::cout << "Flags updated for " << updated << " message(s)\n";
              }
              else
              {
                // Fallback: manually update cache for single UID
                if (!current_mailbox.empty())
                {
                  auto uids = parseUidSet(uid_set);
                  for (uint32_t uid : uids)
                  {
                    // Extract just the flags from the command
                    // E.g., "+FLAGS (\Seen)" -> "\Seen"
                    std::regex extract_flags(R"(\(([^)]*)\))");
                    std::smatch match;
                    if (std::regex_search(flags, match, extract_flags))
                    {
                      cache.updateFlags(current_mailbox, uid, match[1].str());
                    }
                  }
                }
                std::cout << "Flags updated\n";
              }
            }
            else
            {
              std::cout << "STORE failed\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUidCopy(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: copy <uid-set> <destination-mailbox>\n";
      return;
    }

    const std::string& uid_set = args[1];
    const std::string& dest = args[2];

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, dest, &done]() -> asio::awaitable<void>
        {
          try
          {
            auto result = co_await imap_client->asyncUidCopyMail(uid_set, dest);
            if (result.has_value())
            {
              std::cout << "Messages copied to " << dest << "\n";
            }
            else
            {
              std::cout << "COPY failed: " << result.error().toString() << "\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUidMove(const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      std::cout << "Usage: move <uid-set> <destination-mailbox>\n";
      return;
    }

    const std::string& uid_set = args[1];
    const std::string& dest = args[2];

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, dest, &done]() -> asio::awaitable<void>
        {
          try
          {
            auto result = co_await imap_client->asyncUidMove(uid_set, dest);
            if (result.has_value())
            {
              // Remove moved messages from local cache
              if (!current_mailbox.empty())
              {
                auto uids = parseUidSet(uid_set);
                cache.removeMessages(current_mailbox, uids);
                std::cout << "Moved " << uids.size() << " message(s) to " << dest << "\n";
              }
              else
              {
                std::cout << "Messages moved to " << dest << "\n";
              }
            }
            else
            {
              std::cout << "MOVE failed: " << result.error().toString() << "\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleUidExpunge(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: expunge <uid-set>\n";
      return;
    }

    const std::string& uid_set = args[1];

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, &done]() -> asio::awaitable<void>
        {
          try
          {
            auto result = co_await imap_client->asyncUidExpunge(uid_set);
            if (result.has_value())
            {
              std::cout << "Messages expunged\n";
            }
            else
            {
              std::cout << "EXPUNGE failed: " << result.error().toString() << "\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleRead(const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      std::cout << "Usage: read <uid>\n";
      std::cout << "Fetches and displays message body (caches for future access)\n";
      return;
    }

    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected. Use 'select <mailbox>' first.\n";
      return;
    }

    uint32_t uid = 0;
    try
    {
      uid = std::stoul(args[1]);
    }
    catch (...)
    {
      std::cout << "Invalid UID: " << args[1] << "\n";
      return;
    }

    // Check if body is already cached
    if (cache.hasBody(current_mailbox, uid))
    {
      auto msg = cache.getMessage(current_mailbox, uid);
      if (msg)
      {
        displayCachedMessage(*msg);
        return;
      }
    }

    // Fetch body from server
    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid, &done]() -> asio::awaitable<void>
        {
          try
          {
            // Fetch BODY[] which includes headers + body
            std::string uid_str = std::to_string(uid);
            auto result = co_await imap_client->asyncUidFetchMail(uid_str, "(UID FLAGS BODY[])");

            if (result.has_value())
            {
              const std::string& response = result.value();

              // Parse the response to extract body content
              // Format: * N FETCH (UID xxx BODY[] {size}\r\n<content>)\r\n
              std::string text_body, html_body;
              parseBodyResponse(response, text_body, html_body);

              // Update cache with body content
              cache.setMessageBody(current_mailbox, uid, text_body, html_body);

              // Retrieve and display
              auto msg = cache.getMessage(current_mailbox, uid);
              if (msg)
              {
                displayCachedMessage(*msg);
              }
              else
              {
                // Message not in cache yet - add basic entry and display raw
                std::cout << "Message fetched (not cached):\n";
                if (response.length() > 2000)
                {
                  std::cout << response.substr(0, 2000) << "\n... (" << response.length() - 2000 << " more bytes)\n";
                }
                else
                {
                  std::cout << response << "\n";
                }
              }
            }
            else
            {
              std::cout << "FETCH failed: " << result.error().toString() << "\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::displayCachedMessage(const CachedMessage& msg)
  {
    std::cout << "\n";
    std::cout << "══════════════════════════════════════════════════════════════\n";
    std::cout << "Message UID: " << msg.uid << "\n";
    std::cout << "══════════════════════════════════════════════════════════════\n";

    if (!msg.from.empty())
      std::cout << "From:    " << msg.from << "\n";
    if (!msg.to.empty())
      std::cout << "To:      " << msg.to << "\n";
    if (!msg.cc.empty())
      std::cout << "Cc:      " << msg.cc << "\n";
    if (!msg.date.empty())
      std::cout << "Date:    " << msg.date << "\n";
    if (!msg.subject.empty())
      std::cout << "Subject: " << msg.subject << "\n";

    std::cout << "Flags:   " << (msg.flags.empty() ? "(none)" : msg.flags) << "\n";

    if (msg.size > 0)
    {
      if (msg.size < 1024)
      {
        std::cout << "Size:    " << msg.size << " bytes\n";
      }
      else if (msg.size < 1024 * 1024)
      {
        std::cout << "Size:    " << (msg.size / 1024) << " KB\n";
      }
      else
      {
        std::cout << "Size:    " << (msg.size / (1024 * 1024)) << " MB\n";
      }
    }

    if (!msg.attachments.empty())
    {
      std::cout << "Attachments: " << msg.attachments.size() << "\n";
      for (const auto& att : msg.attachments)
      {
        std::cout << "  - " << att.filename << " (" << att.content_type;
        if (att.size > 0)
        {
          if (att.size < 1024)
          {
            std::cout << ", " << att.size << " bytes";
          }
          else
          {
            std::cout << ", " << (att.size / 1024) << " KB";
          }
        }
        std::cout << ")\n";
      }
    }

    std::cout << "──────────────────────────────────────────────────────────────\n";

    if (msg.has_body)
    {
      // Prefer text body for terminal display
      const std::string& body = msg.text_body.empty() ? msg.html_body : msg.text_body;
      if (!body.empty())
      {
        // Show first 3000 chars
        if (body.length() > 3000)
        {
          std::cout << body.substr(0, 3000) << "\n\n... (" << body.length() - 3000 << " more characters)\n";
        }
        else
        {
          std::cout << body << "\n";
        }
      }
      else
      {
        std::cout << "(no body content available)\n";
      }
    }
    else
    {
      std::cout << "(body not cached - use 'read " << msg.uid << "' to fetch)\n";
    }

    std::cout << "══════════════════════════════════════════════════════════════\n\n";
  }

  void ImapCli::parseBodyResponse(const std::string& response, std::string& text_body, std::string& html_body)
  {
    // Extract body content from IMAP FETCH response
    // The body is typically in a literal: BODY[] {size}\r\n<content>

    size_t literal_pos = response.find("BODY[]");
    if (literal_pos == std::string::npos)
    {
      literal_pos = response.find("BODY[TEXT]");
    }
    if (literal_pos == std::string::npos)
    {
      // Try to find any body content
      literal_pos = response.find('{');
    }

    if (literal_pos == std::string::npos)
    {
      // No literal found, try to extract content differently
      return;
    }

    // Find the literal size marker {NNN}
    size_t brace_start = response.find('{', literal_pos);
    if (brace_start == std::string::npos)
      return;

    size_t brace_end = response.find('}', brace_start);
    if (brace_end == std::string::npos)
      return;

    size_t content_size = 0;
    try
    {
      content_size = std::stoul(response.substr(brace_start + 1, brace_end - brace_start - 1));
    }
    catch (...)
    {
      return;
    }

    // Content starts after }\r\n
    size_t content_start = brace_end + 1;
    if (content_start < response.size() && response[content_start] == '\r')
      content_start++;
    if (content_start < response.size() && response[content_start] == '\n')
      content_start++;

    if (content_start + content_size > response.size())
    {
      content_size = response.size() - content_start;
    }

    std::string raw_body = response.substr(content_start, content_size);

    // Use MimeParser to extract text/html parts
    auto parsed = mime::parseMessage(raw_body);
    if (parsed.has_value())
    {
      text_body = parsed->text_body;
      html_body = parsed->html_body;
    }

    // If MIME parsing didn't find structured content, use raw body as text
    if (text_body.empty() && html_body.empty())
    {
      // Try to separate headers from body
      size_t header_end = raw_body.find("\r\n\r\n");
      if (header_end == std::string::npos)
      {
        header_end = raw_body.find("\n\n");
      }

      if (header_end != std::string::npos)
      {
        text_body = raw_body.substr(header_end + (raw_body[header_end] == '\r' ? 4 : 2));
      }
      else
      {
        text_body = raw_body;
      }
    }
  }

  // --- Sync and Headers ---

  void ImapCli::handleSync(const std::vector<std::string>&)
  {
    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected. Use 'select <mailbox>' first.\n";
      return;
    }

    auto modseq_opt = cache.getHighestModSeq(current_mailbox);
    uint64_t last_modseq = modseq_opt.value_or(0);

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, last_modseq, &done]() -> asio::awaitable<void>
        {
          try
          {
            if (last_modseq > 0)
            {
              // Incremental sync using CONDSTORE CHANGEDSINCE
              std::cout << "Incremental sync since MODSEQ " << last_modseq << "...\n";

              // Fetch UID, FLAGS, MODSEQ, and ENVELOPE for changed messages
              auto result =
                  co_await imap_client->asyncUidFetchChangedSince("1:*", last_modseq, "(UID FLAGS MODSEQ ENVELOPE)");

              if (result.has_value())
              {
                size_t updated = parseFetchResponsesAndUpdateCache(result.value());
                if (updated > 0)
                {
                  std::cout << "Updated " << updated << " changed message(s)\n";
                }
                else
                {
                  std::cout << "No changes since last sync\n";
                }
              }
              else
              {
                std::cout << "FETCH failed\n";
              }
            }
            else
            {
              // Full sync - get all UIDs, FLAGS, MODSEQ, and ENVELOPE
              std::cout << "Full sync - fetching all messages...\n";

              auto result = co_await imap_client->asyncUidFetchMail("1:*", "(UID FLAGS RFC822.SIZE MODSEQ ENVELOPE)");

              if (result.has_value())
              {
                size_t cached = parseFetchResponsesAndUpdateCache(result.value());
                std::cout << "Cached " << cached << " message(s)\n";
              }
              else
              {
                std::cout << "FETCH failed\n";
              }
            }

            // Show updated cache stats
            auto new_modseq = cache.getHighestModSeq(current_mailbox);
            if (new_modseq)
            {
              std::cout << "Cache HIGHESTMODSEQ: " << *new_modseq << "\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleFetchHeaders(const std::vector<std::string>& args)
  {
    std::string uid_set = args.size() > 1 ? args[1] : "1:20";

    std::cout << "Fetching headers for UIDs " << uid_set << "...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, uid_set, &done]() -> asio::awaitable<void>
        {
          try
          {
            // Fetch UID, FLAGS, RFC822.SIZE, MODSEQ, and ENVELOPE
            auto result = co_await imap_client->asyncUidFetchMail(uid_set, "(UID FLAGS RFC822.SIZE MODSEQ ENVELOPE)");

            if (result.has_value())
            {
              size_t cached = parseFetchResponsesAndUpdateCache(result.value());

              if (cached > 0)
              {
                std::cout << "Fetched " << cached << " message(s):\n\n";

                // Show message list with envelope data
                auto parsed_uids = parseUidSet(uid_set);
                for (uint32_t uid : parsed_uids)
                {
                  auto msg = cache.getMessage(current_mailbox, uid);
                  if (msg)
                  {
                    // Status indicators
                    std::cout << "  [" << msg->uid << "] ";
                    if (!msg->isSeen())
                      std::cout << "*";  // Unread
                    if (msg->isFlagged())
                      std::cout << "!";  // Flagged
                    if (msg->isAnswered())
                      std::cout << "R";  // Replied
                    std::cout << " ";

                    // Subject (truncated)
                    std::string subj = msg->subject.empty() ? "(no subject)" : msg->subject;
                    if (subj.size() > 50)
                    {
                      subj = subj.substr(0, 47) + "...";
                    }
                    std::cout << subj << "\n";

                    // From
                    if (!msg->from.empty())
                    {
                      std::string from = msg->from;
                      if (from.size() > 55)
                      {
                        from = from.substr(0, 52) + "...";
                      }
                      std::cout << "       From: " << from << "\n";
                    }

                    // Date and size
                    std::cout << "       Date: " << (msg->date.empty() ? "unknown" : msg->date);
                    if (msg->size > 0)
                    {
                      if (msg->size < 1024)
                      {
                        std::cout << "  Size: " << msg->size << " B";
                      }
                      else if (msg->size < 1024 * 1024)
                      {
                        std::cout << "  Size: " << (msg->size / 1024) << " KB";
                      }
                      else
                      {
                        std::cout << "  Size: " << (msg->size / (1024 * 1024)) << " MB";
                      }
                    }
                    std::cout << "\n\n";
                  }
                }
              }
              else
              {
                // Show raw response if parsing failed
                const std::string& resp = result.value();
                if (resp.length() > 500)
                {
                  std::cout << resp.substr(0, 500) << "\n... (" << resp.length() - 500 << " more bytes)\n";
                }
                else
                {
                  std::cout << resp << "\n";
                }
              }
            }
            else
            {
              std::cout << "FETCH failed\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  // --- IDLE ---

  void ImapCli::handleIdle(const std::vector<std::string>& args)
  {
    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected. Use 'select <mailbox>' first.\n";
      return;
    }

    int timeout = args.size() > 1 ? std::stoi(args[1]) : 60;  // Default 60 seconds

    std::cout << "Entering IDLE mode for " << timeout << " seconds (press Enter to exit early)...\n";

    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, timeout, &done]() -> asio::awaitable<void>
        {
          try
          {
            // Start IDLE
            auto start_result = co_await imap_client->asyncIdleStart();
            if (!start_result.has_value())
            {
              std::cout << "Failed to enter IDLE: " << start_result.error().toString() << "\n";
              done.set_value();
              co_return;
            }

            std::cout << "In IDLE mode. Waiting for notifications...\n";
            in_idle = true;

            // Wait for notification
            auto wait_result = co_await imap_client->asyncIdleWait(timeout);
            if (wait_result.has_value())
            {
              std::cout << "Notification received: " << wait_result.value() << "\n";
            }
            else
            {
              std::cout << "IDLE timeout or error\n";
            }

            // Exit IDLE
            auto done_result = co_await imap_client->asyncIdleDone();
            in_idle = false;

            if (done_result.has_value())
            {
              std::cout << "Exited IDLE mode\n";
            }
          }
          catch (const std::exception& e)
          {
            std::cout << "Exception: " << e.what() << "\n";
            in_idle = false;
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  // --- Cache Operations ---

  void ImapCli::handleCacheInfo(const std::vector<std::string>&)
  {
    std::cout << "=== Message Cache ===\n";

    std::cout << "Session: QRESYNC " << (qresync_enabled ? "enabled" : "disabled") << "\n";

    if (current_mailbox.empty())
    {
      std::cout << "No mailbox selected\n";
      return;
    }

    auto uidvalidity = cache.getUidValidity(current_mailbox);
    if (!uidvalidity.has_value())
    {
      std::cout << "No cache for " << current_mailbox << "\n";
      return;
    }

    auto highestmodseq = cache.getHighestModSeq(current_mailbox);
    auto uidnext = cache.getUidNext(current_mailbox);
    size_t msg_count = cache.messageCount(current_mailbox);
    bool qresync_valid = cache.isValidForQresync(current_mailbox);

    std::cout << "\nMailbox: " << current_mailbox << "\n";
    std::cout << "  UIDVALIDITY: " << uidvalidity.value() << "\n";
    std::cout << "  HIGHESTMODSEQ: " << highestmodseq.value_or(0) << "\n";
    if (uidnext)
    {
      std::cout << "  UIDNEXT: " << *uidnext << "\n";
    }
    std::cout << "  Cached messages: " << msg_count << "\n";
    std::cout << "  QRESYNC ready: " << (qresync_valid ? "yes" : "no") << "\n";

    if (msg_count > 0)
    {
      // Show UID ranges (compact form)
      std::string uid_ranges = cache.getKnownUidRanges(current_mailbox);
      std::cout << "  UID ranges: " << uid_ranges << "\n";

      // Count seen/unseen
      auto uids = cache.getCachedUids(current_mailbox);
      size_t seen_count = 0;
      size_t flagged_count = 0;
      size_t with_subject = 0;
      for (uint32_t uid : uids)
      {
        auto msg = cache.getMessage(current_mailbox, uid);
        if (msg)
        {
          if (msg->isSeen())
            seen_count++;
          if (msg->isFlagged())
            flagged_count++;
          if (!msg->subject.empty())
            with_subject++;
        }
      }
      std::cout << "  Status: " << seen_count << " seen, " << (msg_count - seen_count) << " unread";
      if (flagged_count > 0)
      {
        std::cout << ", " << flagged_count << " flagged";
      }
      std::cout << "\n";
      std::cout << "  Envelope data: " << with_subject << "/" << msg_count << " messages\n";

      // Show recent messages with envelope data
      if (with_subject > 0)
      {
        std::cout << "\n  Recent messages:\n";
        size_t shown = 0;
        // Iterate in reverse to show most recent first (highest UIDs)
        for (auto it = uids.rbegin(); it != uids.rend() && shown < 5; ++it)
        {
          auto msg = cache.getMessage(current_mailbox, *it);
          if (msg && !msg->subject.empty())
          {
            std::cout << "    [" << msg->uid << "] ";
            // Show flags
            if (!msg->isSeen())
              std::cout << "*";  // Unread marker
            if (msg->isFlagged())
              std::cout << "!";  // Flagged marker
            std::cout << " ";

            // Truncate subject if too long
            std::string subj = msg->subject;
            if (subj.size() > 45)
            {
              subj = subj.substr(0, 42) + "...";
            }
            std::cout << subj << "\n";

            // Show from
            if (!msg->from.empty())
            {
              std::string from = msg->from;
              if (from.size() > 50)
              {
                from = from.substr(0, 47) + "...";
              }
              std::cout << "         From: " << from << "\n";
            }

            shown++;
          }
        }
      }
    }
  }

  void ImapCli::handleClearCache()
  {
    cache.clearAll();
    std::cout << "Cache cleared\n";
  }

  // --- Utility ---

  void ImapCli::handleCapability()
  {
    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncCapability();
          if (result.has_value())
          {
            std::cout << "Server capabilities:\n";
            for (const auto& cap : result->capabilities)
            {
              std::cout << "  " << cap << "\n";
            }
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::handleNoop()
  {
    std::promise<void> done;
    auto fut = done.get_future();

    asio::co_spawn(
        io_context,
        [this, &done]() -> asio::awaitable<void>
        {
          auto result = co_await imap_client->asyncNoop();
          if (result.has_value())
          {
            std::cout << "NOOP OK (check for any notifications above)\n";
          }
          done.set_value();
        },
        asio::detached);

    waitForOperation(fut);
  }

  void ImapCli::showHelp(const std::vector<std::string>& args)
  {
    if (args.size() > 1)
    {
      showCommandHelp(args[1]);
      return;
    }

    std::cout << "=== IMAP CLI Commands ===\n\n";
    std::cout << "Connection:\n";
    std::cout << "  connect [host] [port]    - Connect to server\n";
    std::cout << "  login <user> <pass>      - Authenticate\n";
    std::cout << "  logout                   - Logout\n";
    std::cout << "\nMailbox:\n";
    std::cout << "  list [pattern]           - List mailboxes\n";
    std::cout << "  lsub [pattern]           - List subscribed mailboxes\n";
    std::cout << "  select <mailbox>         - Select (with CONDSTORE)\n";
    std::cout << "  examine <mailbox>        - Examine (read-only)\n";
    std::cout << "  create <mailbox>         - Create a new mailbox\n";
    std::cout << "  delete <mailbox>         - Delete a mailbox\n";
    std::cout << "  rename <old> <new>       - Rename a mailbox\n";
    std::cout << "  subscribe <mailbox>      - Subscribe to mailbox\n";
    std::cout << "  unsubscribe <mailbox>    - Unsubscribe from mailbox\n";
    std::cout << "  close                    - Close mailbox (expunge)\n";
    std::cout << "  status <mailbox>         - Get status without selecting\n";
    std::cout << "\nMessages:\n";
    std::cout << "  search <criteria>        - Search for messages\n";
    std::cout << "  fetch <uids> [items]     - Fetch message data\n";
    std::cout << "  headers [uids]           - Fetch headers for list view\n";
    std::cout << "  read <uid>               - Read message body (cached)\n";
    std::cout << "  store <uids> <flags>     - Modify flags\n";
    std::cout << "  copy <uids> <mailbox>    - Copy messages\n";
    std::cout << "  move <uids> <mailbox>    - Move messages\n";
    std::cout << "  expunge <uids>           - Delete specific messages\n";
    std::cout << "  expunge-all              - Expunge all deleted\n";
    std::cout << "  append <mbox> <file>     - Upload message from file\n";
    std::cout << "\nSync & Notifications:\n";
    std::cout << "  sync                     - Incremental sync\n";
    std::cout << "  idle [seconds]           - Wait for notifications\n";
    std::cout << "  noop                     - Check for updates\n";
    std::cout << "\nCache:\n";
    std::cout << "  cache                    - Show cache info\n";
    std::cout << "  clear-cache              - Clear cache\n";
    std::cout << "  caps                     - Server capabilities\n";
    std::cout << "\n  help / exit / quit\n";
  }

}  // namespace aurora::mail::cli
