#include <gtest/gtest.h>

#include <ImapTokenizer.hpp>
#include <ImapValue.hpp>
#include <variant>

using aurora::mail::imap::parser::Atom;
using aurora::mail::imap::parser::List;
using aurora::mail::imap::parser::Literal;
using aurora::mail::imap::parser::Nil;
using aurora::mail::imap::parser::Number;
using aurora::mail::imap::parser::Quoted;
using aurora::mail::imap::parser::Tokenizer;
using aurora::mail::imap::parser::Value;

namespace
{
  // Helpers that fail the test with a useful message when the variant
  // doesn't hold the expected alternative.
  template<typename T>
  const T& expectHolds(const Value& v)
  {
    if (!std::holds_alternative<T>(v))
    {
      ADD_FAILURE() << "Value does not hold expected alternative (index=" << v.index() << ")";
    }
    return std::get<T>(v);
  }
}  // namespace

// ---------------------------------------------------------------------------
// Atoms / numbers / NIL
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, AtomReturnsAtomToken)
{
  Tokenizer t("FETCH");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "FETCH");
  EXPECT_EQ(t.position(), 5U);
}

TEST(ImapTokenizer, NumericTokenReturnsNumber)
{
  Tokenizer t("12345");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Number>(*v), 12345);
}

TEST(ImapTokenizer, NegativeNumbersAreParsedAsNumbers)
{
  // IMAP responses don't usually include negative numbers, but the tokenizer
  // accepts a leading '-' as part of an atom-like and from_chars handles it.
  Tokenizer t("-7");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Number>(*v), -7);
}

TEST(ImapTokenizer, NumberWithLetterStaysAtom)
{
  Tokenizer t("12X");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "12X");
}

TEST(ImapTokenizer, NilUppercaseRecognised)
{
  Tokenizer t("NIL");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_TRUE(std::holds_alternative<Nil>(*v));
}

TEST(ImapTokenizer, NilLowercaseRecognised)
{
  Tokenizer t("nil");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_TRUE(std::holds_alternative<Nil>(*v));
}

TEST(ImapTokenizer, MixedCaseNilIsAtomNotNil)
{
  // Only the canonical "NIL" / "nil" are recognised. Mixed case stays atom.
  Tokenizer t("Nil");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "Nil");
}

// ---------------------------------------------------------------------------
// Special single-character tokens
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, AsteriskIsStandaloneAtom)
{
  Tokenizer t("* OK");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "*");
}

TEST(ImapTokenizer, PlusIsStandaloneAtom)
{
  // Continuation request: tokenizer must surface '+' as a token rather than
  // merge it with the rest of the atom.
  Tokenizer t("+ Ready");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "+");
}

// ---------------------------------------------------------------------------
// Quoted strings
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, QuotedStringSimple)
{
  Tokenizer t(R"("hello world")");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Quoted>(*v).value, "hello world");
}

TEST(ImapTokenizer, QuotedStringEmpty)
{
  Tokenizer t(R"("")");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Quoted>(*v).value, "");
}

TEST(ImapTokenizer, QuotedStringWithEscapedQuote)
{
  Tokenizer t(R"("she said \"hi\"")");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Quoted>(*v).value, R"(she said "hi")");
}

TEST(ImapTokenizer, QuotedStringWithEscapedBackslash)
{
  Tokenizer t(R"("a\\b")");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Quoted>(*v).value, R"(a\b)");
}

TEST(ImapTokenizer, QuotedStringUnterminatedReturnsError)
{
  Tokenizer t(R"("oops)");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
  EXPECT_NE(v.error().find("Unterminated"), std::string::npos);
}

TEST(ImapTokenizer, QuotedStringEscapeAtEofReturnsError)
{
  // Trailing '\' with no following character is a malformed escape.
  std::string in = R"("trail\)";
  Tokenizer t(in);
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, EmptyList)
{
  Tokenizer t("()");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& list = expectHolds<std::unique_ptr<List>>(*v);
  ASSERT_NE(list, nullptr);
  EXPECT_TRUE(list->items.empty());
}

TEST(ImapTokenizer, ListOfNumbers)
{
  Tokenizer t("(1 2 3)");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& list = expectHolds<std::unique_ptr<List>>(*v);
  ASSERT_EQ(list->items.size(), 3U);
  EXPECT_EQ(expectHolds<Number>(list->items[0]), 1);
  EXPECT_EQ(expectHolds<Number>(list->items[1]), 2);
  EXPECT_EQ(expectHolds<Number>(list->items[2]), 3);
}

TEST(ImapTokenizer, ListOfAtomsAndQuoted)
{
  // Regression: `(` used to be double-consumed, dropping the first item of
  // every parenthesised list. We pin the fix here so any future "consume"
  // refactor doesn't silently swallow the leading element again.
  Tokenizer t(R"((FETCH "abc" 12))");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& list = expectHolds<std::unique_ptr<List>>(*v);
  ASSERT_EQ(list->items.size(), 3U);
  EXPECT_EQ(expectHolds<Atom>(list->items[0]).value, "FETCH");
  EXPECT_EQ(expectHolds<Quoted>(list->items[1]).value, "abc");
  EXPECT_EQ(expectHolds<Number>(list->items[2]), 12);
}

TEST(ImapTokenizer, NestedLists)
{
  Tokenizer t("(1 (2 3) 4)");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& list = expectHolds<std::unique_ptr<List>>(*v);
  ASSERT_EQ(list->items.size(), 3U);
  EXPECT_EQ(expectHolds<Number>(list->items[0]), 1);
  const auto& inner = expectHolds<std::unique_ptr<List>>(list->items[1]);
  ASSERT_EQ(inner->items.size(), 2U);
  EXPECT_EQ(expectHolds<Number>(inner->items[0]), 2);
  EXPECT_EQ(expectHolds<Number>(inner->items[1]), 3);
  EXPECT_EQ(expectHolds<Number>(list->items[2]), 4);
}

TEST(ImapTokenizer, ListWithExtraWhitespace)
{
  Tokenizer t("(   1    2 )");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& list = expectHolds<std::unique_ptr<List>>(*v);
  ASSERT_EQ(list->items.size(), 2U);
}

TEST(ImapTokenizer, UnclosedListReturnsError)
{
  Tokenizer t("(1 2 ");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
  EXPECT_NE(v.error().find("Unclosed list"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Literals  {N}\r\n<N bytes>
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, LiteralReturnsExactBytes)
{
  Tokenizer t("{5}\r\nhello");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Literal>(*v).data, "hello");
}

TEST(ImapTokenizer, LiteralWithBinaryData)
{
  // Literals are size-delimited, so embedded NUL or CRLF must be preserved.
  std::string in = "{6}\r\nA\0B\r\nC";
  in.assign("{6}\r\nA\0B\r\nC", 12);
  Tokenizer t(in);
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  const auto& lit = expectHolds<Literal>(*v);
  EXPECT_EQ(lit.data.size(), 6U);
  EXPECT_EQ(std::string_view(lit.data.data(), 6), std::string_view("A\0B\r\nC", 6));
}

TEST(ImapTokenizer, LiteralEmpty)
{
  Tokenizer t("{0}\r\n");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Literal>(*v).data, "");
}

TEST(ImapTokenizer, LiteralIncompleteDataIsError)
{
  Tokenizer t("{10}\r\nshort");  // claims 10 bytes, only 5 follow
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
  EXPECT_NE(v.error().find("Incomplete literal"), std::string::npos);
}

TEST(ImapTokenizer, LiteralMissingClosingBraceIsError)
{
  Tokenizer t("{5\r\nhello");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
}

TEST(ImapTokenizer, LiteralWithoutCrlfStillWorks)
{
  // RFC 3501 requires CRLF after the closing brace, but the tokenizer is
  // lenient: it consumes optional \r and \n. We pin the lenient behaviour
  // because real servers occasionally send only \n.
  Tokenizer t("{3}\nabc");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Literal>(*v).data, "abc");
}

TEST(ImapTokenizer, LiteralWithoutAnyTerminatorReturnsBytes)
{
  // No CRLF at all -- the tokenizer should still extract N bytes.
  Tokenizer t("{3}xyz");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Literal>(*v).data, "xyz");
}

// ---------------------------------------------------------------------------
// Whitespace and EOF
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, EofReturnsError)
{
  Tokenizer t("");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
  EXPECT_EQ(v.error(), "End of stream");
}

TEST(ImapTokenizer, OnlyWhitespaceIsEof)
{
  Tokenizer t("   \t  ");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value());
  EXPECT_EQ(v.error(), "End of stream");
}

TEST(ImapTokenizer, SkipSpacesDoesNotEatNewlines)
{
  // skipSpaces must NOT step over CR/LF - those are line terminators.
  Tokenizer t(" \tA\r\nB");
  t.skipSpaces();
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "A");
  // After reading A, position is at \r. skipSpaces does not advance.
  t.skipSpaces();
  EXPECT_EQ(t.position(), 3U);
}

TEST(ImapTokenizer, SkipLineEndingsConsumesCrAndLf)
{
  Tokenizer t("\r\n\r\nABC");
  t.skipLineEndings();
  EXPECT_EQ(t.position(), 4U);
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "ABC");
}

TEST(ImapTokenizer, SequentialTokensAdvancePosition)
{
  Tokenizer t("A 1 NIL");
  auto a = t.nextValue();
  auto b = t.nextValue();
  auto c = t.nextValue();
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(expectHolds<Atom>(*a).value, "A");
  EXPECT_EQ(expectHolds<Number>(*b), 1);
  EXPECT_TRUE(std::holds_alternative<Nil>(*c));
}

// ---------------------------------------------------------------------------
// Atom delimiters – behavioural pins.
// ---------------------------------------------------------------------------

TEST(ImapTokenizer, AtomBreaksOnPercentSign)
{
  Tokenizer t("FOO%BAR");
  auto v = t.nextValue();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(expectHolds<Atom>(*v).value, "FOO");
}

TEST(ImapTokenizer, BackslashIsAtomBreaker_FlagsCannotBeTokenisedDirectly)
{
  // INCONSISTENCY: RFC 3501 defines IMAP system flags as "\Seen", "\Answered",
  // etc. -- the leading backslash is part of the token. The current
  // readAtomLike() treats '\\' as a delimiter, so the tokenizer reports an
  // error when asked to read a flag value. The downstream parser works around
  // this by leaving FETCH FLAGS data unparsed, but any future caller that
  // tries to walk the parsed list will see an empty atom / parser error.
  //
  // We assert the *current* (broken) behaviour so the inconsistency is
  // visible in CI; flipping this expectation is the marker that a fix has
  // landed in the tokenizer.
  Tokenizer t(R"(\Seen)");
  auto v = t.nextValue();
  ASSERT_FALSE(v.has_value()) << "Expected tokenizer to fail on flag input due to '\\' being an atom delimiter; "
                                 "if this assertion now succeeds, ImapTokenizer has been fixed and the test "
                                 "should be updated to assert the new (correct) behaviour.";
}

TEST(ImapTokenizer, FlagsListInsideParensCurrentlyFails)
{
  // Same inconsistency, but exercised through the more realistic path:
  // FETCH responses regularly carry parenthesised flag lists like
  //   (\Seen \Answered)
  // and the tokenizer fails as soon as it tries to read the inner '\\Seen'.
  Tokenizer t(R"((\Seen \Answered))");
  auto v = t.nextValue();
  EXPECT_FALSE(v.has_value());
}
