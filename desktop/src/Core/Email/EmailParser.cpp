#include "EmailParser.hpp"

#include "TextSanitizer.hpp"

#include <QRegularExpression>
#include <QDebug>

using namespace aurora::mail::common::mail;
namespace mime_reader = aurora::mail::common::mime::reader;

namespace aurora::mail::app::email {

using aurora::mail::app::utils::TextSanitizer;

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
