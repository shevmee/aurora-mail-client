#ifndef IMAP_COMMAND_HPP
#define IMAP_COMMAND_HPP

#include <Base64.hpp>
#include <CommandValidation.hpp>
#include <ProtocolConcepts.hpp>
#include <ProtocolError.hpp>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <variant>

namespace aurora::mail::imap::command
{

  using aurora::mail::common::ProtocolError;
  using aurora::mail::common::Result;
  using aurora::mail::common::validateNoCrlf;

  /**
   * @brief Quote a mailbox name for IMAP if it contains special characters.
   *
   * IMAP requires mailbox names to be quoted if they contain spaces,
   * special characters, or start with a digit. This function only handles
   * QUOTED-STRING form; literal {N} form is the caller's responsibility for
   * mailboxes containing CR/LF (which validateNoCrlf already rejects upstream).
   */
  inline std::string quoteMailbox(std::string_view mailbox)
  {
    auto isSpecialChar = [](unsigned char c) constexpr
    {
      return c == ' ' || c == '"' || c == '\\' ||
             c == '(' || c == ')' || c == '{' || c == '}' ||
             c == '[' || c == ']' ||
             c < 32 || c > 126;
    };

    std::size_t extra = 0;
    bool needsQuoting = false;

    for (unsigned char c : mailbox)
    {
      if (isSpecialChar(c))
      {
        needsQuoting = true;
      }
      if (c == '"' || c == '\\') {
        extra += 1;
      }
    }

    if (!needsQuoting)
    {
      return std::string{mailbox};
    }

    std::string quoted;
    quoted.reserve(mailbox.size() + extra + 2);
    quoted.push_back('"');

    for (unsigned char c : mailbox)
    {
      if (c == '"' || c == '\\')
      {
        quoted.push_back('\\');
      }
      quoted.push_back(static_cast<char>(c));
    }
    quoted.push_back('"');
    return quoted;
  }

  /**
   * @brief Common validators for tag + (optional) extra fields.
   *
   * IMAP commands all begin with a TAG; centralize that single check so each
   * command's serialize() body is small.
   */
  namespace detail
  {
    [[nodiscard]] inline std::expected<void, ProtocolError> validateTag(std::string_view tag)
    {
      return validateNoCrlf(tag, "IMAP tag");
    }
  }  // namespace detail

  /**
   * @brief IMAP command structures
   *
   * IMAP commands follow the pattern: TAG COMMAND [ARGS]
   * All commands need a tag for tracking responses.
   *
   * serialize() returns Result<std::string> so input validation (CRLF
   * injection guard, etc.) propagates through the existing std::expected
   * pipeline.
   */

  struct Capability
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} CAPABILITY\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "CAPABILITY";
    }
  };

  struct Login
  {
    std::string tag;
    std::string username;
    std::string password;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(username, "LOGIN username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(password, "LOGIN password"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} LOGIN \"{}\" \"{}\"\r\n", tag, username, password);
    }
    static constexpr std::string_view name() noexcept
    {
      return "LOGIN";
    }
  };

  /**
   * @brief AUTHENTICATE PLAIN: tag + base64 SASL IR (RFC 4959).
   */
  struct AuthPlain
  {
    std::string tag;
    std::string username;
    std::string password;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(username, "AUTHENTICATE PLAIN username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(password, "AUTHENTICATE PLAIN password"); !v)
      {
        return std::unexpected(v.error());
      }
      std::string payload;
      payload.reserve(2 + username.size() + password.size());
      payload += '\0';
      payload += username;
      payload += '\0';
      payload += password;
      std::string encoded = aurora::mail::common::base64::base64Encode(payload);
      return std::format("{} AUTHENTICATE PLAIN {}\r\n", tag, encoded);
    }
    static constexpr std::string_view name() noexcept
    {
      return "AUTHENTICATE PLAIN";
    }
  };

  /**
   * @brief AUTHENTICATE XOAUTH2: pure value object carrying an already-acquired
   * token.
   *
   * Token acquisition is performed by the caller (typically the UI / OAuth
   * manager). serialize() emits the SASL-IR initial-response form
   * (RFC 4959), but ImapClient::asyncAuthenticate also handles servers that
   * reply with a "+" continuation containing a base64 error blob.
   */
  struct AuthXOAuth2
  {
    std::string tag;
    std::string username;
    std::string access_token;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(username, "AUTHENTICATE XOAUTH2 username"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(access_token, "AUTHENTICATE XOAUTH2 access_token"); !v)
      {
        return std::unexpected(v.error());
      }
      if (access_token.empty())
      {
        return std::unexpected(
            ProtocolError::auth("AUTHENTICATE XOAUTH2: access token is empty",
                                "Acquire a valid token before constructing AuthXOAuth2"));
      }
      std::string sasl;
      sasl.reserve(20 + username.size() + access_token.size());
      sasl += "user=";
      sasl += username;
      sasl += "\1auth=Bearer ";
      sasl += access_token;
      sasl += "\1\1";
      std::string encoded = aurora::mail::common::base64::base64Encode(sasl);
      return std::format("{} AUTHENTICATE XOAUTH2 {}\r\n", tag, encoded);
    }
    static constexpr std::string_view name() noexcept
    {
      return "AUTHENTICATE XOAUTH2";
    }
  };

  struct Logout
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} LOGOUT\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "LOGOUT";
    }
  };

  struct Select
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "SELECT mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} SELECT {}\r\n", tag, quoteMailbox(mailbox));
    }
    static constexpr std::string_view name() noexcept
    {
      return "SELECT";
    }
  };

  struct Examine
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "EXAMINE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} EXAMINE {}\r\n", tag, quoteMailbox(mailbox));
    }
    static constexpr std::string_view name() noexcept
    {
      return "EXAMINE";
    }
  };

  struct Fetch
  {
    std::string tag;
    std::string message_set;
    std::string data_items;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(message_set, "FETCH message_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(data_items, "FETCH data_items"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} FETCH {} {}\r\n", tag, message_set, data_items);
    }
    static constexpr std::string_view name() noexcept
    {
      return "FETCH";
    }
  };

  struct Store
  {
    std::string tag;
    std::string message_set;
    std::string flags;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(message_set, "STORE message_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(flags, "STORE flags"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} STORE {} {}\r\n", tag, message_set, flags);
    }
    static constexpr std::string_view name() noexcept
    {
      return "STORE";
    }
  };

  struct Search
  {
    std::string tag;
    std::string criteria;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(criteria, "SEARCH criteria"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} SEARCH {}\r\n", tag, criteria);
    }
    static constexpr std::string_view name() noexcept
    {
      return "SEARCH";
    }
  };

  struct List
  {
    std::string tag;
    std::string reference;
    std::string mailbox_pattern;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(reference, "LIST reference"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox_pattern, "LIST mailbox_pattern"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} LIST \"{}\" \"{}\"\r\n", tag, reference, mailbox_pattern);
    }
    static constexpr std::string_view name() noexcept
    {
      return "LIST";
    }
  };

  struct Noop
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} NOOP\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "NOOP";
    }
  };

  struct StartTls
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} STARTTLS\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "STARTTLS";
    }
  };

  struct UidFetch
  {
    std::string tag;
    std::string uid_set;
    std::string data_items;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID FETCH uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(data_items, "UID FETCH data_items"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID FETCH {} {}\r\n", tag, uid_set, data_items);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID FETCH";
    }
  };

  struct UidStore
  {
    std::string tag;
    std::string uid_set;
    std::string flags;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID STORE uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(flags, "UID STORE flags"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID STORE {} {}\r\n", tag, uid_set, flags);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID STORE";
    }
  };

  struct UidSearch
  {
    std::string tag;
    std::string criteria;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(criteria, "UID SEARCH criteria"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID SEARCH {}\r\n", tag, criteria);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID SEARCH";
    }
  };

  struct UidCopy
  {
    std::string tag;
    std::string uid_set;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID COPY uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "UID COPY mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID COPY {} {}\r\n", tag, uid_set, quoteMailbox(mailbox));
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID COPY";
    }
  };

  struct UidExpunge
  {
    std::string tag;
    std::string uid_set;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID EXPUNGE uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID EXPUNGE {}\r\n", tag, uid_set);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID EXPUNGE";
    }
  };

  struct Status
  {
    std::string tag;
    std::string mailbox;
    std::string status_items;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "STATUS mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(status_items, "STATUS items"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} STATUS \"{}\" ({})\r\n", tag, mailbox, status_items);
    }
    static constexpr std::string_view name() noexcept
    {
      return "STATUS";
    }
  };

  struct Expunge
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} EXPUNGE\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "EXPUNGE";
    }
  };

  struct Close
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} CLOSE\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "CLOSE";
    }
  };

  struct Idle
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} IDLE\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "IDLE";
    }
  };

  // Note: IdleDone is NOT a tagged command - it's a continuation
  struct IdleDone
  {
    [[nodiscard]] Result<std::string> serialize() const
    {
      return std::string{ "DONE\r\n" };
    }
    static constexpr std::string_view name() noexcept
    {
      return "DONE";
    }
  };

  struct SelectCondstore
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "SELECT (CONDSTORE) mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} SELECT {} (CONDSTORE)\r\n", tag, quoteMailbox(mailbox));
    }
    static constexpr std::string_view name() noexcept
    {
      return "SELECT (CONDSTORE)";
    }
  };

  struct ExamineCondstore
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "EXAMINE (CONDSTORE) mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} EXAMINE {} (CONDSTORE)\r\n", tag, quoteMailbox(mailbox));
    }
    static constexpr std::string_view name() noexcept
    {
      return "EXAMINE (CONDSTORE)";
    }
  };

  struct UidFetchChangedSince
  {
    std::string tag;
    std::string uid_set;
    std::string data_items;
    uint64_t modseq;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID FETCH (CHANGEDSINCE) uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(data_items, "UID FETCH (CHANGEDSINCE) data_items"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID FETCH {} {} (CHANGEDSINCE {})\r\n", tag, uid_set, data_items, modseq);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID FETCH (CHANGEDSINCE)";
    }
  };

  struct UidStoreUnchangedSince
  {
    std::string tag;
    std::string uid_set;
    std::string flags;
    uint64_t modseq;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID STORE (UNCHANGEDSINCE) uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(flags, "UID STORE (UNCHANGEDSINCE) flags"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID STORE {} (UNCHANGEDSINCE {}) {}\r\n", tag, uid_set, modseq, flags);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID STORE (UNCHANGEDSINCE)";
    }
  };

  struct SelectQresync
  {
    std::string tag;
    std::string mailbox;
    uint32_t uidvalidity;
    uint64_t modseq;
    std::string known_uids;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "SELECT (QRESYNC) mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(known_uids, "SELECT (QRESYNC) known_uids"); !v)
      {
        return std::unexpected(v.error());
      }
      if (known_uids.empty())
      {
        return std::format("{} SELECT {} (QRESYNC ({} {}))\r\n", tag, quoteMailbox(mailbox), uidvalidity, modseq);
      }
      return std::format(
          "{} SELECT {} (QRESYNC ({} {} {}))\r\n", tag, quoteMailbox(mailbox), uidvalidity, modseq, known_uids);
    }
    static constexpr std::string_view name() noexcept
    {
      return "SELECT (QRESYNC)";
    }
  };

  struct EnableQresync
  {
    std::string tag;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} ENABLE QRESYNC\r\n", tag);
    }
    static constexpr std::string_view name() noexcept
    {
      return "ENABLE QRESYNC";
    }
  };

  // === Mailbox Management ===

  struct Create
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "CREATE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} CREATE \"{}\"\r\n", tag, mailbox);
    }
    static constexpr std::string_view name() noexcept
    {
      return "CREATE";
    }
  };

  struct Delete
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "DELETE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} DELETE \"{}\"\r\n", tag, mailbox);
    }
    static constexpr std::string_view name() noexcept
    {
      return "DELETE";
    }
  };

  struct Rename
  {
    std::string tag;
    std::string old_name;
    std::string new_name;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(old_name, "RENAME old_name"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(new_name, "RENAME new_name"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} RENAME \"{}\" \"{}\"\r\n", tag, old_name, new_name);
    }
    static constexpr std::string_view name() noexcept
    {
      return "RENAME";
    }
  };

  struct Subscribe
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "SUBSCRIBE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} SUBSCRIBE \"{}\"\r\n", tag, mailbox);
    }
    static constexpr std::string_view name() noexcept
    {
      return "SUBSCRIBE";
    }
  };

  struct Unsubscribe
  {
    std::string tag;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "UNSUBSCRIBE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UNSUBSCRIBE \"{}\"\r\n", tag, mailbox);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UNSUBSCRIBE";
    }
  };

  struct Lsub
  {
    std::string tag;
    std::string reference;
    std::string mailbox_pattern;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(reference, "LSUB reference"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox_pattern, "LSUB mailbox_pattern"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} LSUB \"{}\" \"{}\"\r\n", tag, reference, mailbox_pattern);
    }
    static constexpr std::string_view name() noexcept
    {
      return "LSUB";
    }
  };

  // === APPEND (upload message) ===
  //
  // serialize() emits the APPEND command line with a literal {N} declaration.
  // The actual message body is sent by ImapClient after the server responds
  // with a "+" continuation. The message itself may legitimately contain
  // CRLFs (it is line-oriented), so it is excluded from validateNoCrlf —
  // the literal {N} length protocol-frames it correctly.
  struct Append
  {
    std::string tag;
    std::string mailbox;
    std::string flags;
    std::string date_time;
    std::string message;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "APPEND mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(flags, "APPEND flags"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(date_time, "APPEND date_time"); !v)
      {
        return std::unexpected(v.error());
      }
      std::string cmd = std::format("{} APPEND \"{}\"", tag, mailbox);
      if (!flags.empty())
      {
        cmd += " ";
        cmd += flags;
      }
      if (!date_time.empty())
      {
        cmd += " \"";
        cmd += date_time;
        cmd += "\"";
      }
      cmd += std::format(" {{{}}}\r\n", message.size());
      return cmd;
    }
    static constexpr std::string_view name() noexcept
    {
      return "APPEND";
    }
  };

  // === UID MOVE (RFC 6851) ===
  struct UidMove
  {
    std::string tag;
    std::string uid_set;
    std::string mailbox;

    [[nodiscard]] Result<std::string> serialize() const
    {
      if (auto v = detail::validateTag(tag); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(uid_set, "UID MOVE uid_set"); !v)
      {
        return std::unexpected(v.error());
      }
      if (auto v = validateNoCrlf(mailbox, "UID MOVE mailbox"); !v)
      {
        return std::unexpected(v.error());
      }
      return std::format("{} UID MOVE {} \"{}\"\r\n", tag, uid_set, mailbox);
    }
    static constexpr std::string_view name() noexcept
    {
      return "UID MOVE";
    }
  };

  using Command = aurora::mail::common::CommandChecker<
      Capability,
      Login,
      AuthPlain,
      AuthXOAuth2,
      Logout,
      Select,
      Examine,
      Fetch,
      Store,
      Search,
      List,
      Noop,
      StartTls,
      UidFetch,
      UidStore,
      UidSearch,
      UidCopy,
      UidExpunge,
      Status,
      Expunge,
      Close,
      Idle,
      SelectCondstore,
      ExamineCondstore,
      UidFetchChangedSince,
      UidStoreUnchangedSince,
      SelectQresync,
      EnableQresync,
      Create,
      Delete,
      Rename,
      Subscribe,
      Unsubscribe,
      Lsub,
      Append,
      UidMove>::type;

  using AuthVariant = std::variant<Login, AuthPlain, AuthXOAuth2>;

  /**
   * @brief Render an IMAP command (variant) to wire bytes.
   */
  inline Result<std::string> serialize(const Command& cmd)
  {
    return std::visit([](const auto& c) -> Result<std::string> { return c.serialize(); }, cmd);
  }

  inline Result<std::string> serialize(const AuthVariant& auth)
  {
    return std::visit([](const auto& a) -> Result<std::string> { return a.serialize(); }, auth);
  }

  template<aurora::mail::common::Serializable T>
  inline Result<std::string> serialize(const T& cmd)
  {
    return cmd.serialize();
  }
}  // namespace aurora::mail::imap::command

#endif  // IMAP_COMMAND_HPP
