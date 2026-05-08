#include "TextSanitizer.hpp"

#include <QRegularExpression>
#include <QVector>

namespace aurora::mail::app::utils
{

  namespace
  {
    void stripSurrogatePairsInPlace(QString& s)
    {
      QString out;
      out.reserve(s.size());
      for (int i = 0; i < s.size(); ++i)
      {
        const QChar c = s.at(i);
        if (c.isHighSurrogate())
        {
          if (i + 1 < s.size() && s.at(i + 1).isLowSurrogate())
          {
            ++i;
          }
          continue;
        }
        if (c.isLowSurrogate())
        {
          continue;
        }
        out.append(c);
      }
      s = std::move(out);
    }
  }  // namespace

  QString TextSanitizer::removeSupplementaryPlaneCharacters(const QString& text)
  {
    QString copy = text;
    stripSurrogatePairsInPlace(copy);
    return copy;
  }

  QString TextSanitizer::sanitizeEmailBody(const QString& html)
  {
    QString processedHtml = html;
    fixInvalidCssColors(processedHtml);
    stripSurrogatePairsInPlace(processedHtml);
    return processedHtml;
  }

  QString TextSanitizer::sanitizePlainText(const QString& input)
  {
    QString result;
    result.reserve(input.length());

    // =========================================================================
    // Unicode Category Whitelist Approach
    // =========================================================================
    // Instead of hardcoding problematic ranges (fragile and incomplete),
    // we use QChar's semantic categories to allow only "safe" characters.
    //
    // This is more maintainable because:
    // 1. Unicode categories are standardized and well-documented
    // 2. QChar handles the complex mapping for us
    // 3. Easy to adjust by adding/removing categories
    // 4. Automatically handles new Unicode versions

    for (const QChar& ch : input)
    {
      // Skip surrogate pairs entirely (primary emoji removal mechanism)
      // QString's range-for properly handles UTF-16, so we can check directly
      if (ch.isSurrogate())
      {
        continue;
      }

      // Filter based on Unicode category
      switch (ch.category())
      {
        // === ALLOWED CATEGORIES ===
        // Letters (all scripts: Latin, Cyrillic, Arabic, CJK, etc.)
        case QChar::Letter_Uppercase:
        case QChar::Letter_Lowercase:
        case QChar::Letter_Titlecase:
        case QChar::Letter_Modifier:
        case QChar::Letter_Other:

        // Combining marks (e.g. decomposed é = e + U+0301) — required for
        // correct plain-text display after NFC/NFD from various senders.
        case QChar::Mark_NonSpacing:
        case QChar::Mark_SpacingCombining:

        // Numbers
        case QChar::Number_DecimalDigit:
        case QChar::Number_Letter:  // Roman numerals, etc.
        case QChar::Number_Other:

        // Punctuation (essential for text readability)
        case QChar::Punctuation_Connector:  // Underscore, etc.
        case QChar::Punctuation_Dash:
        case QChar::Punctuation_Open:          // (, [, {
        case QChar::Punctuation_Close:         // ), ], }
        case QChar::Punctuation_InitialQuote:  // «, "
        case QChar::Punctuation_FinalQuote:    // », "
        case QChar::Punctuation_Other:         // !, ?, ., etc.

        // Symbols (selective - avoid problematic ones)
        case QChar::Symbol_Math:      // +, =, <, >, etc.
        case QChar::Symbol_Currency:  // $, €, ¥, etc.
        // Deliberately EXCLUDED:
        // - Symbol_Modifier: Often invisible combining characters
        // - Symbol_Other: Contains emoji and problematic symbols

        // Whitespace
        case QChar::Separator_Space:
        case QChar::Separator_Line:
        case QChar::Separator_Paragraph: result.append(ch); break;

        // === SPECIAL HANDLING ===
        case QChar::Other_Format:
          // Format characters are usually invisible modifiers.
          // Only allow BOM (Byte Order Mark) at the very start
          if (ch.unicode() == 0xFEFF && result.isEmpty())
          {
            result.append(ch);
          }
          // Reject everything else (zero-width joiner, direction marks, etc.)
          break;

        case QChar::Other_Control:
          // Control characters are generally dangerous, but we need
          // standard whitespace for text formatting
          if (ch == '\t' || ch == '\n' || ch == '\r')
          {
            result.append(ch);
          }
          // Reject all other control chars (including null bytes)
          break;

        // === REJECTED CATEGORIES ===
        // Everything else is filtered out:
        // - Mark_NonSpacing, Mark_SpacingCombining, Mark_Enclosing
        // - Other_PrivateUse (custom fonts, undefined behavior)
        // - Other_Surrogate (already handled above)
        // - Other_NotAssigned
        // - Separator_Paragraph (sometimes problematic)
        default:
          // Special case: Unicode Replacement Character → '?'
          if (ch.unicode() == 0xFFFD)
          {
            result.append('?');
          }
          // Everything else: silently drop
          break;
      }
    }

    return result;
  }

  void TextSanitizer::fixInvalidCssColors(QString& html)
  {
    // =========================================================================
    // Problem: Qt's CSS parser is strict about hex color formats
    // =========================================================================
    // Many email clients generate invalid CSS
    // like #RGBA (4 hex digits) which Qt doesn't recognize, causing:
    // - "Unknown color name '#0000'" warnings in console
    // - Incorrect rendering (colors ignored)
    //
    // This is a Qt-specific workaround, not a general HTML sanitization task.

    // -------------------------------------------------------------------------
    // Fix 1: 4-character hex colors (#RGBA)
    // -------------------------------------------------------------------------
    // Pattern explanation:
    // - Captures 4 hex digits
    // - Negative lookahead (?![0-9A-Fa-f]) ensures it's exactly 4 digits
    //   (prevents matching the first 4 digits of a valid 6 or 8 digit color)
    static const QRegularExpression fourCharColor(R"(#([0-9A-Fa-f])([0-9A-Fa-f])([0-9A-Fa-f])([0-9A-Fa-f])(?![0-9A-Fa-f]))");

    // We need to process matches in reverse order to preserve string offsets
    // (replacing characters changes positions of subsequent matches)
    struct Replacement
    {
      int position;
      int length;
      QString newValue;
    };
    QVector<Replacement> replacements;

    auto it = fourCharColor.globalMatch(html);
    while (it.hasNext())
    {
      auto match = it.next();
      QString alpha = match.captured(4);
      QString replacement;

      if (alpha == QStringLiteral("0"))
      {
        // Fully transparent (alpha = 0) → CSS 'transparent' keyword
        replacement = QStringLiteral("transparent");
      }
      else
      {
        // Non-zero alpha → Convert to #RGB (drop alpha channel)
        // Qt will automatically expand #RGB to #RRGGBB
        replacement = QStringLiteral("#") + match.captured(1) + match.captured(2) + match.captured(3);
      }

      replacements.append(
          { static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), replacement });
    }

    // Apply replacements in reverse order (from end to start)
    // This way, earlier positions aren't affected by later replacements
    for (int i = replacements.size() - 1; i >= 0; --i)
    {
      html.replace(replacements[i].position, replacements[i].length, replacements[i].newValue);
    }

    // -------------------------------------------------------------------------
    // Fix 2: 8-character colors with zero alpha (#RRGGBBAA where AA=00)
    // -------------------------------------------------------------------------
    // These are valid in CSS Color Module Level 4, but Qt's older parser
    // doesn't support them. Convert to 'transparent' for consistency.
    static const QRegularExpression eightCharTransparent(R"(#[0-9A-Fa-f]{6}00(?![0-9A-Fa-f]))");
    html.replace(eightCharTransparent, QStringLiteral("transparent"));
  }

}  // namespace aurora::mail::app::utils
