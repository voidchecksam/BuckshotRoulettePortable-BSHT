// JSON string values with their escapes decoded. The raw readers return the
// characters between the quotes as written, so the runtime export's Label text
// "GET UP...\nTHE NIGHT..." arrived with a literal backslash-n, and an escaped
// quote ended the string early.
#include "core/json_lite.h"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string Decoded(const std::string& doc, const char* key) {
    return core::json::ReadStringTopLevelDecoded(doc.data(), doc.data() + doc.size(), key);
}

}  // namespace

TEST(JsonDecode, NewlineAndTabEscapesBecomeCharacters) {
    EXPECT_EQ(Decoded(R"({"text":"AB\nC\tD"})", "text"), "AB\nC\tD");
}

TEST(JsonDecode, AnEscapedQuoteDoesNotEndTheString) {
    EXPECT_EQ(Decoded(R"({"text":"say \"hi\" now","next":"x"})", "text"), "say \"hi\" now");
}

TEST(JsonDecode, BackslashAndSlashEscapes) {
    EXPECT_EQ(Decoded(R"({"p":"a\\b\/c"})", "p"), "a\\b/c");
}

TEST(JsonDecode, AsciiUnicodeEscapes) {
    EXPECT_EQ(Decoded(R"({"t":"A."})", "t"), "A.");
}

TEST(JsonDecode, OnlyTheTopLevelKeyIsRead) {
    EXPECT_EQ(Decoded(R"({"exports":{"text":"inner"},"text":"outer"})", "text"), "outer");
}

TEST(JsonDecode, AMissingKeyIsEmpty) {
    EXPECT_EQ(Decoded(R"({"a":"b"})", "text"), "");
}
