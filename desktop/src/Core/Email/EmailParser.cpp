#include "EmailParser.hpp"

#include "TextSanitizer.hpp"

#include <QRegularExpression>
#include <QDebug>

using namespace aurora::mail::common::mail;
namespace mime_reader = aurora::mail::common::mime::reader;

namespace aurora::mail::app::email {

using aurora::mail::app::utils::TextSanitizer;

namespace {

/**
 * Tokenize an IMAP ENVELOPE-style content string into its top-level fields.
 *
 * Each field is one of:
 *   - "quoted string" (with backslash-escaped quotes)
 *   - NIL
 *   - (parenthesized group)  — returned with its outer parens
 *
 * The envelope passed in MUST already have its outermost parentheses removed.
 * Nested parens and quoted strings are tracked so address-list groups are
 * returned intact. Used to safely extract the TO field (index 5) from an
 * IMAP ENVELOPE without relying on fragile global regex matches.
 */
QStringList tokenizeEnvelopeFields(const QString& env)
{
    QStringList fields;
    int i = 0;
    auto skipWs = [&]() {
        while (i < env.length() && env[i].isSpace()) {
            ++i;
        }
    };

    while (i < env.length()) {
        skipWs();
        if (i >= env.length()) {
            break;
        }
        const QChar c = env[i];

        if (c == QLatin1Char('"')) {
            const int start = i;
            ++i;
            while (i < env.length()) {
                if (env[i] == QLatin1Char('\\') && i + 1 < env.length()) {
                    i += 2;
                    continue;
                }
                if (env[i] == QLatin1Char('"')) {
                    ++i;
                    break;
                }
                ++i;
            }
            fields << env.mid(start, i - start);
        } else if (c == QLatin1Char('(')) {
            const int start = i;
            int depth = 1;
            ++i;
            while (i < env.length() && depth > 0) {
                if (env[i] == QLatin1Char('"')) {
                    ++i;
                    while (i < env.length()) {
                        if (env[i] == QLatin1Char('\\') && i + 1 < env.length()) {
                            i += 2;
                            continue;
                        }
                        if (env[i] == QLatin1Char('"')) {
                            ++i;
                            break;
                        }
                        ++i;
                    }
                    continue;
                }
                if (env[i] == QLatin1Char('(')) {
                    ++depth;
                } else if (env[i] == QLatin1Char(')')) {
                    --depth;
                }
                ++i;
            }
            fields << env.mid(start, i - start);
        } else if (env.mid(i, 3) == QStringLiteral("NIL")
                   && (i + 3 == env.length() || !env[i + 3].isLetterOrNumber())) {
            fields << QStringLiteral("NIL");
            i += 3;
        } else {
            // Skip any unexpected character defensively.
            ++i;
        }
    }
    return fields;
}

/** Strip surrounding double-quotes and unescape \" / \\ from an envelope token. */
QString unquoteEnvelopeString(QString s)
{
    if (s.length() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) {
        s = s.mid(1, s.length() - 2);
        s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return s;
}

/**
 * Decode an envelope-encoded display string. Applies RFC 2047 decoding when
 * the value contains an encoded-word marker; otherwise returns it as-is.
 */
QString decodeEnvelopeDisplay(const QString& raw)
{
    if (raw.contains(QStringLiteral("=?"))) {
        return QString::fromStdString(mime_reader::decodeHeaderValue(raw.toStdString()));
    }
    return raw;
}

/**
 * Given an IMAP envelope address-list field (e.g. `((name adl mailbox host)…)`
 * or `NIL`), return the first address rendered as a display string. Prefers
 * the personal name; falls back to "user@host"; returns empty when neither is
 * available. Any additional recipients are summarised as " + N".
 */
QString extractFirstAddressFromList(const QString& field)
{
    if (field.isEmpty() || field == QStringLiteral("NIL")) {
        return QString();
    }
    QString inner = field;
    if (inner.startsWith(QLatin1Char('('))) {
        inner = inner.mid(1);
    }
    if (inner.endsWith(QLatin1Char(')'))) {
        inner.chop(1);
    }

    const QStringList addrs = tokenizeEnvelopeFields(inner);
    if (addrs.isEmpty()) {
        return QString();
    }

    QString first = addrs.front();
    if (first.startsWith(QLatin1Char('('))) {
        first = first.mid(1);
    }
    if (first.endsWith(QLatin1Char(')'))) {
        first.chop(1);
    }

    const QStringList parts = tokenizeEnvelopeFields(first);
    if (parts.size() < 4) {
        return QString();
    }

    const QString rawName = (parts[0] == QStringLiteral("NIL")) ? QString() : unquoteEnvelopeString(parts[0]);
    const QString user    = (parts[2] == QStringLiteral("NIL")) ? QString() : unquoteEnvelopeString(parts[2]);
    const QString host    = (parts[3] == QStringLiteral("NIL")) ? QString() : unquoteEnvelopeString(parts[3]);

    QString head;
    if (!rawName.isEmpty()) {
        head = decodeEnvelopeDisplay(rawName);
    } else if (!user.isEmpty() && !host.isEmpty()) {
        head = QStringLiteral("%1@%2").arg(user, host);
    } else {
        return QString();
    }

    if (addrs.size() > 1) {
        head += QStringLiteral(" + %1").arg(addrs.size() - 1);
    }
    return head;
}

/** Render a MailAddress as its display name when present, else the address. */
QString formatMailAddressDisplay(const aurora::mail::common::mail::MailAddress& addr)
{
    return QString::fromStdString(addr.getName().empty() ? addr.getAddress() : addr.getName());
}

/**
 * Build a "To: …" display string from an EmailRecipients struct. Returns the
 * first address' display form, with "+ N" appended when more than one
 * recipient exists. Empty when there are no recipients.
 */
QString formatRecipientsDisplay(const aurora::mail::common::mail::EmailRecipients& rec)
{
    if (rec.to.empty()) {
        return QString();
    }
    QString head = formatMailAddressDisplay(rec.to.front());
    if (rec.to.size() > 1) {
        head += QStringLiteral(" + %1").arg(rec.to.size() - 1);
    }
    return head;
}

}  // namespace

EmailSummary EmailSummary::fromReceivedMessage(
    const ReceivedMailMessage& msg,
    const QString& uid)
{
    EmailSummary summary;
    summary.uid = uid;
    summary.subject = QString::fromStdString(msg.subject);
    
    // Format sender
    summary.from = QString::fromStdString(
        msg.from.getName().empty()
            ? msg.from.getAddress()
            : msg.from.getName());

    summary.to = formatRecipientsDisplay(msg.email_recipients);
    
    // Convert date
    if (msg.has_date) {
        auto time_t = std::chrono::system_clock::to_time_t(msg.date);
        summary.date = QDateTime::fromSecsSinceEpoch(time_t);
    } else {
        summary.date = QDateTime::currentDateTime();
    }
    
    // Create preview from text body
    if (!msg.text_body.empty()) {
        QString text = QString::fromStdString(msg.text_body);
        summary.preview = text.left(150).simplified();
    } else if (!msg.html_body.empty()) {
        // Strip HTML tags for preview
        QString html = QString::fromStdString(msg.html_body);
        html.remove(QRegularExpression("<[^>]*>"));
        summary.preview = html.left(150).simplified();
    }
    
    summary.isRead = msg.flags.seen;
    summary.hasAttachments = msg.hasAttachments();
    
    return summary;
}

QVector<EmailSummary> EmailParser::parseEmailList(const std::string& response)
{
    QVector<EmailSummary> emails;
    QString responseStr = QString::fromStdString(response);
    
    // Parse IMAP FETCH responses for ENVELOPE data
    // Format: * seqnum FETCH (UID num FLAGS (...) ENVELOPE (...))
    QStringList fetchResponses = responseStr.split(QRegularExpression(R"(\* \d+ FETCH )"));
    
    qDebug() << "[EmailParser] Found" << (fetchResponses.size() - 1) << "FETCH responses";
    
    for (int i = 1; i < fetchResponses.size(); ++i) {
        QString fetchData = fetchResponses[i];
        EmailSummary summary;
        
        // Extract UID
        QRegularExpression uidRegex(R"(UID (\d+))");
        auto uidMatch = uidRegex.match(fetchData);
        if (!uidMatch.hasMatch()) continue;
        summary.uid = uidMatch.captured(1);
        
        // Extract FLAGS
        QRegularExpression flagsRegex(R"(FLAGS \(([^)]*)\))");
        auto flagsMatch = flagsRegex.match(fetchData);
        summary.isRead = flagsMatch.hasMatch() && flagsMatch.captured(1).contains("\\Seen");
        
        // Extract ENVELOPE
        int envStart = fetchData.indexOf("ENVELOPE (");
        if (envStart != -1) {
            envStart += 10;
            
            // Find envelope end by matching parentheses
            int parenDepth = 1;
            int envEnd = envStart;
            while (envEnd < fetchData.length() && parenDepth > 0) {
                QChar c = fetchData[envEnd];
                if (c == '(') parenDepth++;
                else if (c == ')') parenDepth--;
                envEnd++;
            }
            
            QString envelope = fetchData.mid(envStart, envEnd - envStart - 1);
            
            // Parse date (first quoted string)
            int firstQuote = envelope.indexOf('"');
            if (firstQuote != -1) {
                int dateEnd = envelope.indexOf('"', firstQuote + 1);
                if (dateEnd != -1) {
                    QString dateStr = envelope.mid(firstQuote + 1, dateEnd - firstQuote - 1);
                    summary.date = QDateTime::fromString(dateStr, Qt::RFC2822Date);
                    if (!summary.date.isValid()) {
                        summary.date = QDateTime::currentDateTime();
                    }
                    
                    // Subject (second quoted string)
                    int subjectStart = envelope.indexOf('"', dateEnd + 1);
                    if (subjectStart != -1) {
                        int subjectEnd = envelope.indexOf('"', subjectStart + 1);
                        if (subjectEnd != -1) {
                            QString rawSubject = envelope.mid(subjectStart + 1, subjectEnd - subjectStart - 1);
                            // Use library's decoder for MIME encoded words
                            if (rawSubject.contains("=?")) {
                                summary.subject = QString::fromStdString(
                                    mime_reader::decodeHeaderValue(rawSubject.toStdString()));
                            } else {
                                summary.subject = rawSubject;
                            }
                        }
                    }
                }
            }
            
            // Extract FROM address
            QRegularExpression fromRegex(R"(\(\((?:\"([^\"]*)\"|NIL) (?:\"[^\"]*\"|NIL) (?:\"([^\"]*)\"|NIL) (?:\"([^\"]*)\"|NIL)\)\))");
            auto fromMatch = fromRegex.match(envelope);
            if (fromMatch.hasMatch()) {
                QString name = fromMatch.captured(1);
                QString user = fromMatch.captured(2);
                QString host = fromMatch.captured(3);
                
                if (!name.isEmpty()) {
                    // Use library's decoder for MIME encoded names
                    if (name.contains("=?")) {
                        summary.from = QString::fromStdString(
                            mime_reader::decodeHeaderValue(name.toStdString()));
                    } else {
                        summary.from = name;
                    }
                } else if (!user.isEmpty() && !host.isEmpty()) {
                    summary.from = QString("%1@%2").arg(user, host);
                } else {
                    summary.from = "Unknown";
                }
            } else {
                summary.from = "Unknown sender";
            }
            
            // Extract Message-ID (last quoted field in ENVELOPE)
            QRegularExpression msgIdRegex(R"(<[^>]+>)");
            auto msgIdMatch = msgIdRegex.match(envelope, envelope.lastIndexOf('<'));
            if (msgIdMatch.hasMatch()) {
                summary.messageId = msgIdMatch.captured(0);
            }

            // Extract TO address-list (envelope field index 5: date, subject,
            // from, sender, reply-to, TO, cc, bcc, in-reply-to, message-id).
            // Used by the mail list when displaying outgoing folders (Sent /
            // Drafts) where the recipient is the meaningful label.
            const QStringList envFields = tokenizeEnvelopeFields(envelope);
            if (envFields.size() > 5) {
                summary.to = extractFirstAddressFromList(envFields[5]);
            }
        } else {
            summary.subject = QString("Email #%1").arg(summary.uid);
            summary.from = "Unknown sender";
            summary.date = QDateTime::currentDateTime();
        }
        
        if (summary.subject.isEmpty()) {
            summary.subject = QString("Email #%1").arg(summary.uid);
        }
        
        emails.append(summary);
    }
    
    qDebug() << "[EmailParser] Parsed" << emails.size() << "emails";
    return emails;
}

std::string EmailParser::extractBodyFromFetch(const std::string& response)
{
    // Extract raw body from IMAP FETCH response
    // Format: BODY[] {size}\r\n<content>
    
    size_t bodyPos = response.find("BODY[]");
    if (bodyPos == std::string::npos) {
        bodyPos = response.find("BODY[TEXT]");
    }
    if (bodyPos == std::string::npos) {
        qDebug() << "[EmailParser] No BODY[] or BODY[TEXT] found in response";
        return "";
    }
    
    // Find literal size marker {NNN}
    size_t braceStart = response.find('{', bodyPos);
    if (braceStart == std::string::npos) {
        qDebug() << "[EmailParser] No literal size marker found";
        return "";
    }
    
    size_t braceEnd = response.find('}', braceStart);
    if (braceEnd == std::string::npos) return "";
    
    size_t contentSize = 0;
    try {
        contentSize = std::stoul(response.substr(braceStart + 1, braceEnd - braceStart - 1));
    } catch (...) {
        return "";
    }
    
    // Content starts after }\r\n
    size_t contentStart = braceEnd + 1;
    if (contentStart < response.size() && response[contentStart] == '\r') contentStart++;
    if (contentStart < response.size() && response[contentStart] == '\n') contentStart++;
    
    if (contentStart + contentSize > response.size()) {
        contentSize = response.size() - contentStart;
    }
    
    std::string rawContent = response.substr(contentStart, contentSize);
    
    // Strip leading \r\n that Gmail sometimes adds before headers
    while (!rawContent.empty() && (rawContent[0] == '\r' || rawContent[0] == '\n')) {
        rawContent.erase(0, 1);
    }
    
    qDebug() << "[EmailParser] Extracted body size:" << rawContent.size();
    return rawContent;
}

std::optional<ReceivedMailMessage> EmailParser::parseRawMessage(const std::string& rawMessage)
{
    if (rawMessage.empty()) {
        return std::nullopt;
    }
    
    auto result = mime_reader::parseMessage(rawMessage);
    if (result.has_value()) {
        qDebug() << "[EmailParser] MIME parse successful - subject:" 
                 << QString::fromStdString(result->subject)
                 << "text_body size:" << result->text_body.size()
                 << "html_body size:" << result->html_body.size()
                 << "attachments:" << result->attachments.size();
        return result.value();
    }
    
    qWarning() << "[EmailParser] MIME parse failed:" 
               << QString::fromStdString(result.error().toString());
    return std::nullopt;
}

std::tuple<QString, QString, QString> EmailParser::parseEmailContent(const std::string& response)
{
    // Extract raw message from IMAP response
    std::string rawBody = extractBodyFromFetch(response);
    if (rawBody.empty()) {
        qWarning() << "[EmailParser] Could not extract body from FETCH response";
        return {"", "", ""};
    }
    
    // Use library's MIME parser (GMime-based, handles all MIME complexity)
    auto parsed = parseRawMessage(rawBody);
    
    if (parsed) {
        QString subject = QString::fromStdString(parsed->subject);
        
        // Format sender
        QString from;
        if (!parsed->from.getName().empty()) {
            from = QString("%1 <%2>").arg(
                QString::fromStdString(parsed->from.getName()),
                QString::fromStdString(parsed->from.getAddress()));
        } else {
            from = QString::fromStdString(parsed->from.getAddress());
        }
        
        // Get body (prefer HTML, library already decoded everything)
        QString body;
        if (!parsed->html_body.empty()) {
            body = QString::fromStdString(parsed->html_body);
        } else if (!parsed->text_body.empty()) {
            const QString plain = TextSanitizer::sanitizePlainText(
                QString::fromStdString(parsed->text_body));
            body = formatPlainTextAsHtml(plain);
        } else {
            body = formatPlainTextAsHtml("(No message body)");
        }
        
        return {subject, from, body};
    }
    
    // GMime parsing failed - return empty (don't try manual parsing,
    // as GMime is already the most robust MIME parser available)
    qWarning() << "[EmailParser] Could not parse message";
    return {"(Parse error)", "", formatPlainTextAsHtml("Could not parse email content.")};
}

ParsedEmailContent EmailParser::parseFullEmailContent(const std::string& response)
{
    ParsedEmailContent result;
    
    // Extract raw message from IMAP response
    std::string rawBody = extractBodyFromFetch(response);
    if (rawBody.empty()) {
        qWarning() << "[EmailParser] Could not extract body from FETCH response";
        return result;
    }
    
    // Use library's MIME parser (GMime-based)
    auto parsed = parseRawMessage(rawBody);
    
    if (parsed) {
        result.subject = QString::fromStdString(parsed->subject);
        
        // Format sender
        if (!parsed->from.getName().empty()) {
            result.from = QString("%1 <%2>").arg(
                QString::fromStdString(parsed->from.getName()),
                QString::fromStdString(parsed->from.getAddress()));
        } else {
            result.from = QString::fromStdString(parsed->from.getAddress());
        }
        
        // Get body (prefer HTML)
        if (!parsed->html_body.empty()) {
            result.body = QString::fromStdString(parsed->html_body);
            result.isHtml = true;
        } else if (!parsed->text_body.empty()) {
            const QString plain = TextSanitizer::sanitizePlainText(
                QString::fromStdString(parsed->text_body));
            result.body = formatPlainTextAsHtml(plain);
            result.isHtml = true;
        } else {
            result.body = formatPlainTextAsHtml("(No message body)");
        }
        
        // Extract attachments
        for (const auto& att : parsed->attachments) {
            AttachmentInfo info;
            info.filename = QString::fromStdString(att.filename);
            info.contentType = QString::fromStdString(att.content_type);
            info.size = static_cast<qint64>(att.data.size());
            info.data = QByteArray(reinterpret_cast<const char*>(att.data.data()), 
                                   static_cast<int>(att.data.size()));
            info.isInline = att.is_inline;
            result.attachments.append(info);
            
            qDebug() << "[EmailParser] Found attachment:" << info.filename 
                     << "size:" << info.size << "bytes"
                     << "inline:" << info.isInline;
        }
        
        return result;
    }
    
    // GMime parsing failed
    qWarning() << "[EmailParser] Could not parse message";
    result.body = formatPlainTextAsHtml("Could not parse email content.");
    return result;
}

QString EmailParser::formatPlainTextAsHtml(const QString& plainText)
{
    QString html = plainText;
    
    // Escape HTML entities
    html.replace("&", "&amp;");
    html.replace("<", "&lt;");
    html.replace(">", "&gt;");
    
    // Convert URLs to links
    QRegularExpression urlRegex(R"((https?://[^\s<>"']+))");
    html.replace(urlRegex, R"(<a href="\1" style="color: #4f9cf9;">\1</a>)");
    
    // Convert emails to mailto links
    QRegularExpression emailRegex(R"(([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}))");
    html.replace(emailRegex, R"(<a href="mailto:\1" style="color: #4f9cf9;">\1</a>)");
    
    // Convert line breaks
    html.replace("\r\n", "<br>");
    html.replace("\n", "<br>");
    
    // Wrap in styled container (dark text: reader pane uses light "paper" background)
    return QString(
        "<html><head><style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        "       font-size: 14px; line-height: 1.6; color: #14171a; background: transparent; "
        "       padding: 16px; margin: 0; }"
        "a { color: #1d9bf0; }"
        "</style></head><body>%1</body></html>"
    ).arg(html);
}

} // namespace aurora::mail::app::email
