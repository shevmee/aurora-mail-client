#include <gtest/gtest.h>

#include <QString>
#include <Utils/TextSanitizer.hpp>

#include "QtTestSupport.hpp"

using aurora::mail::app::utils::TextSanitizer;

// ---------------------------------------------------------------------------
// removeSupplementaryPlaneCharacters
// ---------------------------------------------------------------------------

TEST(TextSanitizer, RemovesSurrogatePairsButKeepsBmp)
{
  // U+1F600 (GRINNING FACE) is encoded as the surrogate pair D83D DE00.
  // Plain ASCII and BMP characters must survive untouched.
  QString in = QStringLiteral("Hi! ");
  in.append(QChar(0xD83D));
  in.append(QChar(0xDE00));
  in.append(QStringLiteral(" привіт"));
  const QString out = TextSanitizer::removeSupplementaryPlaneCharacters(in);
  EXPECT_EQ(out, QStringLiteral("Hi!  привіт"));
}

TEST(TextSanitizer, DropsLoneSurrogates)
{
  // A high surrogate not followed by a low surrogate, and a stray low
  // surrogate, must both be removed.
  QString in;
  in.append('A');
  in.append(QChar(0xD83D));
  in.append('B');
  in.append(QChar(0xDE00));
  in.append('C');
  EXPECT_EQ(TextSanitizer::removeSupplementaryPlaneCharacters(in), QStringLiteral("ABC"));
}

TEST(TextSanitizer, EmptyInputIsEmpty)
{
  EXPECT_EQ(TextSanitizer::removeSupplementaryPlaneCharacters(QString()), QString());
}

// ---------------------------------------------------------------------------
// sanitizePlainText  (Unicode category whitelist)
// ---------------------------------------------------------------------------

TEST(TextSanitizer, SanitizePlainTextKeepsAsciiAndCyrillic)
{
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QStringLiteral("Hello, world!")), QStringLiteral("Hello, world!"));
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QStringLiteral("Привіт!")), QStringLiteral("Привіт!"));
}

TEST(TextSanitizer, SanitizePlainTextKeepsCommonWhitespaceControlChars)
{
  // Tab, LF, CR are explicitly allowed; nothing else from C0 controls is.
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QStringLiteral("a\tb\nc\rd")), QStringLiteral("a\tb\nc\rd"));
}

TEST(TextSanitizer, SanitizePlainTextDropsControlChars)
{
  QString in = QStringLiteral("OK");
  in.insert(1, QChar(0x07));  // BEL
  in.insert(3, QChar(0x00));  // NUL
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("OK"));
}

TEST(TextSanitizer, SanitizePlainTextStripsAllSurrogates)
{
  QString in = QStringLiteral("a + b ");
  in.append(QChar(0xD83D));
  in.append(QChar(0xDE00));
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("a + b "));
}

TEST(TextSanitizer, INCONSISTENCY_AsciiCaretIsStrippedAsSymbolModifier)
{
  // INCONSISTENCY (real bug, found by this test): the sanitizer's
  // category whitelist deliberately rejects Unicode category
  // QChar::Symbol_Modifier, with the source comment claiming this
  // category contains "Often invisible combining characters". In fact
  // U+005E (CIRCUMFLEX ACCENT, plain ASCII '^') is in Symbol_Modifier,
  // and so is the grave accent U+0060, the macron U+00AF, and a number
  // of other VISIBLE characters that users routinely type in plain
  // text -- e.g. "e=mc^2", "x^2", "2^10", or shell prompts using `cmd`.
  //
  // The current behaviour is therefore to silently delete '^' and '`'
  // from incoming plain-text email bodies. This is almost certainly
  // not what the author of the comment intended.
  //
  // Pinning this here so any future fix (e.g. allow Symbol_Modifier and
  // instead block specific invisible combining characters by codepoint)
  // is intentional and accompanied by a test update.
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QStringLiteral("e=mc^2")), QStringLiteral("e=mc2"));
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QStringLiteral("`code`")), QStringLiteral("code"));
}

TEST(TextSanitizer, SanitizePlainTextReplacesU_FFFD_WithQuestionMark)
{
  QString in;
  in.append('a');
  in.append(QChar(0xFFFD));
  in.append('b');
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("a?b"));
}

TEST(TextSanitizer, SanitizePlainTextDropsZeroWidthJoiner)
{
  // U+200D (ZERO WIDTH JOINER) is in category Other_Format, which is
  // rejected unless it's a leading BOM. It should not survive the filter.
  QString in;
  in.append('a');
  in.append(QChar(0x200D));
  in.append('b');
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("ab"));
}

TEST(TextSanitizer, SanitizePlainTextKeepsLeadingBom)
{
  QString in;
  in.append(QChar(0xFEFF));
  in.append(QStringLiteral("hello"));
  QString out = TextSanitizer::sanitizePlainText(in);
  ASSERT_FALSE(out.isEmpty());
  EXPECT_EQ(out.at(0), QChar(0xFEFF));
}

TEST(TextSanitizer, SanitizePlainTextDropsMidStringBom)
{
  QString in = QStringLiteral("hello");
  in.append(QChar(0xFEFF));
  in.append(QStringLiteral("world"));
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("helloworld"));
}

TEST(TextSanitizer, SanitizePlainTextKeepsCommonSymbolsAndCurrency)
{
  // Math (+, =, <), currency (€, $, ¥), connector punctuation (_).
  EXPECT_EQ(
      TextSanitizer::sanitizePlainText(QStringLiteral("a+b=c<d $5 €10 ¥3 _x")), QStringLiteral("a+b=c<d $5 €10 ¥3 _x"));
}

TEST(TextSanitizer, SanitizePlainTextDropsPrivateUseArea)
{
  QString in = QStringLiteral("safe");
  in.insert(2, QChar(0xE000));  // first PUA codepoint
  EXPECT_EQ(TextSanitizer::sanitizePlainText(in), QStringLiteral("safe"));
}

TEST(TextSanitizer, SanitizePlainTextEmptyIsEmpty)
{
  EXPECT_EQ(TextSanitizer::sanitizePlainText(QString()), QString());
}

// ---------------------------------------------------------------------------
// sanitizeEmailBody (CSS color fixes + surrogate strip)
// ---------------------------------------------------------------------------

TEST(TextSanitizer, FixesFourCharColorWithZeroAlphaToTransparent)
{
  const QString in = QStringLiteral("<span style=\"color: #abc0\">hi</span>");
  const QString out = TextSanitizer::sanitizeEmailBody(in);
  EXPECT_NE(out.indexOf(QStringLiteral("transparent")), -1) << out.toStdString();
  EXPECT_EQ(out.indexOf(QStringLiteral("#abc0")), -1) << "bad #RGBA color leaked through: " << out.toStdString();
}

TEST(TextSanitizer, FixesFourCharColorWithNonzeroAlphaByDroppingAlpha)
{
  const QString in = QStringLiteral("<span style=\"color: #1A2F\">hi</span>");
  const QString out = TextSanitizer::sanitizeEmailBody(in);
  EXPECT_NE(out.indexOf(QStringLiteral("#1A2")), -1) << out.toStdString();
  EXPECT_EQ(out.indexOf(QStringLiteral("#1A2F")), -1) << "alpha digit leaked through";
}

TEST(TextSanitizer, FixesEightCharColorWithZeroAlphaToTransparent)
{
  const QString in = QStringLiteral("<span style=\"background:#aabbcc00\">hi</span>");
  const QString out = TextSanitizer::sanitizeEmailBody(in);
  EXPECT_NE(out.indexOf(QStringLiteral("transparent")), -1);
  EXPECT_EQ(out.indexOf(QStringLiteral("#aabbcc00")), -1);
}

TEST(TextSanitizer, EightCharColorWithNonzeroAlphaIsLeftAlone)
{
  // Non-zero alpha 8-digit colors (#RRGGBBAA where AA != 00) are not
  // rewritten -- only the AA=00 case is. Pinned.
  const QString in = QStringLiteral("p { color:#11223380; }");
  EXPECT_EQ(TextSanitizer::sanitizeEmailBody(in), in);
}

TEST(TextSanitizer, ValidThreeOrSixDigitColorsArePreserved)
{
  // These are valid Qt-supported formats and must NOT be rewritten.
  const QString in = QStringLiteral("a=#abc; b=#123456; c=#FFF;");
  EXPECT_EQ(TextSanitizer::sanitizeEmailBody(in), in);
}

TEST(TextSanitizer, SanitizeEmailBodyAlsoStripsSurrogatePairs)
{
  QString in = QStringLiteral("<p>");
  in.append(QChar(0xD83D));
  in.append(QChar(0xDE00));
  in.append(QStringLiteral("</p>"));
  EXPECT_EQ(TextSanitizer::sanitizeEmailBody(in), QStringLiteral("<p></p>"));
}

TEST(TextSanitizer, SanitizeEmailBodyHandlesMultipleColors)
{
  // Multiple replacements in a single pass. The implementation walks
  // matches in reverse order to keep offsets stable; verify all matches
  // are rewritten and no source-position drift occurs.
  const QString in =
      QStringLiteral("<a style=\"color:#abc0\">x</a><b style=\"color:#ABC0\">y</b><c style=\"color:#111F\">z</c>");
  const QString out = TextSanitizer::sanitizeEmailBody(in);
  // First two #abc0 -> transparent (case-insensitive); third #111F -> #111
  EXPECT_EQ(out.count(QStringLiteral("transparent")), 2);
  EXPECT_NE(out.indexOf(QStringLiteral("#111")), -1);
  EXPECT_EQ(out.indexOf(QStringLiteral("#111F")), -1);
}

TEST(TextSanitizer, FourCharRegexDoesNotMatchSixCharColor)
{
  // The negative lookahead in the regex is critical: #RRGGBB must NOT be
  // treated as #RGBA + trailing "BB". Pinned.
  const QString in = QStringLiteral("color:#abcdef;");
  EXPECT_EQ(TextSanitizer::sanitizeEmailBody(in), in);
}

TEST(TextSanitizer, EightCharZeroAlphaRegexDoesNotMatchNineHexDigits)
{
  // Negative lookahead (?![0-9A-Fa-f]) prevents matching the prefix of a
  // longer hex run.
  const QString in = QStringLiteral("color:#aabbcc00d;");
  EXPECT_EQ(TextSanitizer::sanitizeEmailBody(in), in);
}
