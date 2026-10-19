// The small JSON reader.
//
// FindKey scans forward and returns the first match anywhere in the span,
// nested objects included. Godot's JSON.stringify emits dictionary keys
// alphabetically, so an animation player serialises as
// {animations:[...], autoplay, path, root}, and FindKey reading "path" from the
// player's span finds a nested track's path, because `animations` sorts first.
//
// These pin both behaviours so the distinction stays deliberate.
#include <gtest/gtest.h>

#include "core/json_lite.h"

#include <cstring>

using namespace core::json;

namespace {
const char* kNested =
    R"({"animations":[{"path":"nested/track:position","name":"inner"}],)"
    R"("autoplay":"","path":"the/player","root":"the/root","count":7})";

std::pair<const char*, const char*> Span_() {
    return {kNested, kNested + std::strlen(kNested)};
}
}  // namespace

TEST(JsonLite, PlainFindKeyReachesIntoNestedObjects) {
    // Documented, not a bug, but it is why the top-level variant exists.
    const auto s = Span_();
    EXPECT_EQ(ReadString(s.first, s.second, "path"), "nested/track:position");
}

TEST(JsonLite, TopLevelFindKeyIgnoresNestedOccurrences) {
    const auto s = Span_();
    EXPECT_EQ(ReadStringTopLevel(s.first, s.second, "path"), "the/player");
    EXPECT_EQ(ReadStringTopLevel(s.first, s.second, "root"), "the/root");
    EXPECT_EQ(ReadIntTopLevel(s.first, s.second, "count", -1), 7);
}

TEST(JsonLite, TopLevelDoesNotFindAKeyThatOnlyExistsNested) {
    const auto s = Span_();
    // "name" lives only inside the nested animation object.
    EXPECT_EQ(ReadStringTopLevel(s.first, s.second, "name"), "");
    EXPECT_EQ(ReadIntTopLevel(s.first, s.second, "name", -42), -42);
}

TEST(JsonLite, KeyMatchingAnchorsOnTheWholeName) {
    // "type" must not match "componentType".
    const char* j = R"({"componentType":5126,"type":"VEC3"})";
    EXPECT_EQ(ReadStringTopLevel(j, j + std::strlen(j), "type"), "VEC3");
    EXPECT_EQ(ReadIntTopLevel(j, j + std::strlen(j), "componentType", 0), 5126);
}

TEST(JsonLite, StringsContainingBracesDoNotBreakDepthTracking) {
    // A brace inside a NAME would otherwise unbalance the depth counter and
    // make every later key invisible.
    const char* j = R"({"path":"weird{name}here","count":3})";
    EXPECT_EQ(ReadStringTopLevel(j, j + std::strlen(j), "path"), "weird{name}here");
    EXPECT_EQ(ReadIntTopLevel(j, j + std::strlen(j), "count", -1), 3);
}

TEST(JsonLite, MissingKeysReturnTheFallback) {
    const auto s = Span_();
    EXPECT_EQ(ReadStringTopLevel(s.first, s.second, "nope"), "");
    EXPECT_EQ(ReadIntTopLevel(s.first, s.second, "nope", 99), 99);
    EXPECT_DOUBLE_EQ(ReadNumberTopLevel(s.first, s.second, "nope", 1.5), 1.5);
}
