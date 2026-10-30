// morse_table.h — 欧文モールス符号表 (Arduino 非依存)
#pragma once
#include <string.h>

namespace cw {

struct MorseEntry {
  char ch;
  const char* code;
};

inline const MorseEntry* morseTable(int& count) {
  static const MorseEntry table[] = {
      {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},    {'E', "."},
      {'F', "..-."},   {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},
      {'K', "-.-"},    {'L', ".-.."},   {'M', "--"},     {'N', "-."},     {'O', "---"},
      {'P', ".--."},   {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
      {'U', "..-"},    {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},   {'Y', "-.--"},
      {'Z', "--.."},   {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
      {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},  {'8', "---.."},
      {'9', "----."},  {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
      {'=', "-...-"},  {'+', ".-.-."},  {'-', "-....-"}, {'(', "-.--."},  {')', "-.--.-"},
      {'\'', ".----."}, {':', "---..."}, {';', "-.-.-."}, {'"', ".-..-."}, {'@', ".--.-."},
      {'!', "-.-.--"}, {'_', "..--.-"}, {'&', ".-..."},
  };
  count = sizeof(table) / sizeof(table[0]);
  return table;
}

// 文字 → 符号 ("." と "-" の列)。未対応なら nullptr
inline const char* morseEncode(char c) {
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  int n;
  const MorseEntry* t = morseTable(n);
  for (int i = 0; i < n; i++)
    if (t[i].ch == c) return t[i].code;
  return nullptr;
}

// 符号 → 文字。未対応なら '?'
inline char morseDecode(const char* code) {
  int n;
  const MorseEntry* t = morseTable(n);
  for (int i = 0; i < n; i++)
    if (strcmp(t[i].code, code) == 0) return t[i].ch;
  return '?';
}

}  // namespace cw
