// TranslationServer: tr(key) looks the key up in the current locale's table,
// which tools/godot/export_dialogue.gd harvested from the game's compiled
// .translation files: one assets/extracted/dialogue_<code>.json per locale,
// holding what Godot displays in that locale. Player-facing text lives there,
// never in src/ (no_game_content_in_binary).
#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace godot {

// Replaces the loaded table. False when the file is missing or malformed; the
// previous table is then cleared, so tr() returns keys.
bool load_translations(const char* path);

// Object::tr: the translated message, or the key itself when there is none.
// By value: the fallback is the argument, which a reference would outlive.
std::string tr(const std::string& key);

// GDScript's String % operator (ustring.cpp String::sprintf) for the %s and
// %% forms the scripts use: each %s takes the next argument in order.
std::string format(std::string_view fmt, std::initializer_list<std::string_view> args);

// Where set_locale finds dialogue_<code>.json (the data root).
void set_translation_root(const std::string& directory);

// "ES LATAM" -> "es_latam": the sheet's locale name as the exporter spells its file.
std::string locale_file_code(const std::string& locale);

// TranslationServer.set_locale. Loads that locale's table; false (and the
// current table and locale kept) when it has none.
bool set_locale(const std::string& locale);

// TranslationServer.get_locale: the locale of the loaded table ("" before any).
std::string get_locale();

// PORT: the port's own text after one message, in every locale: the port's
// credit closing the original's "MAIN CREDITS" (app/port_branding.h). Kept
// across set_locale; "" removes it. Bumps the generation so Labels re-translate.
// Not game content: the original's message still comes from its table.
void set_message_suffix(const std::string& key, const std::string& suffix);

// UTF-8 helpers for the character counts Godot's String gives in UTF-32:
// the number of code points, and the byte length of the first `chars` of them.
int utf8_length(std::string_view text);
std::size_t utf8_prefix_bytes(std::string_view text, int chars);

// Changes every time a table loads (load_translations or set_locale): the
// port's stand-in for NOTIFICATION_TRANSLATION_CHANGED, so a Label set before
// the table loaded re-translates its text (label.cpp:380).
unsigned translation_generation();

}  // namespace godot
