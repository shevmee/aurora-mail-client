#ifndef CORE_UTILS_TEXT_SANITIZER_HPP
#define CORE_UTILS_TEXT_SANITIZER_HPP

#include <QString>

namespace aurora::mail::app::utils {

/**
 * @class TextSanitizer
 * @brief Utilities for sanitizing text content for safe display in Qt.
 *
 * This class addresses real production issues encountered on macOS:
 * - CoreText crashes in CTFontDrawGlyphs/CopyEmojiImage when rendering emoji
 * - Qt's CSS parser errors on non-standard hex color formats (#RGBA, #RRGGBBAA)
 * - Malformed Unicode sequences that cause rendering artifacts
 *
 * Design principles:
 * - Do not round-trip real MIME HTML through QTextDocument::toHtml() (layout loss)
 * - Fix Qt-incompatible CSS colors in raw HTML strings
 * - Use Unicode categories (whitelist) for plain-text sanitization
 */
class TextSanitizer {
public:
    /**
     * @brief Prepares email HTML for QTextBrowser without destroying layout.
     *
     * Real MIME HTML must not be round-tripped through QTextDocument::toHtml():
     * Qt re-serializes to its own subset and breaks tables, divs, and newsletter
     * layouts. For those bodies, only CSS fixes are applied.
     *
     * Plain-text bodies are converted to HTML in EmailParser after
     * sanitizePlainText(); no document round-trip is needed here.
     *
     * @param html Source HTML from the parser (MIME HTML or our plain-text wrapper).
     */
    [[nodiscard]] static QString sanitizeEmailBody(const QString& html);

    /**
     * @brief Removes UTF-16 surrogate pairs (supplementary plane), stripping most color emoji.
     *
     * Used for HTML bodies and list-row snippets: avoids macOS CoreText/ImageIO crashes
     * in CTFontDrawGlyphs / CopyEmojiImage when QTextBrowser paints HTML.
     */
    [[nodiscard]] static QString removeSupplementaryPlaneCharacters(const QString& text);

    /**
     * @brief Sanitizes plain text using Unicode category whitelisting.
     *
     * Filters characters based on their Unicode semantic categories,
     * which is more reliable and maintainable than hardcoded ranges.
     *
     * Automatically removes:
     * - All surrogate pairs (emoji and characters > U+FFFF)
     * - Control characters (except tab, newline, carriage return)
     * - Private use area characters
     * - Most symbols (keeps math, currency, common punctuation)
     *
     * @param input Raw text string.
     * @return Sanitized text with problematic characters removed.
     */
    [[nodiscard]] static QString sanitizePlainText(const QString& input);

private:
    TextSanitizer() = delete; // Static utility class - prevent instantiation

    /**
     * @brief Fixes invalid CSS color formats in HTML.
     *
     * Qt's CSS parser only supports:
     * - #RGB (3 hex digits)
     * - #RRGGBB (6 hex digits)
     * - #RRGGBBAA (8 hex digits)
     * - Named colors and 'transparent'
     *
     * Many emails contain invalid 4-character colors (#RGBA) which cause
     * "Unknown color name" warnings and rendering issues.
     *
     * This method converts:
     * - #RGBA where A=0 → 'transparent'
     * - #RGBA where A≠0 → #RGB (drops alpha, Qt will expand)
     * - #RRGGBBAA where AA=00 → 'transparent'
     *
     * @param html HTML content (modified in-place for performance).
     */
    static void fixInvalidCssColors(QString& html);
};

} // namespace aurora::mail::app::utils

#endif // CORE_UTILS_TEXT_SANITIZER_HPP

