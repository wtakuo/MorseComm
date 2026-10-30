// morse_table.h — 欧文モールス符号表 (Arduino 非依存)
#pragma once
#include <string.h>

namespace cw {

// 解読できなかった符号を表す文字 (表示側で赤い '?' にする)。正しく受信した '?' と区別するため
constexpr char CHAR_UNKNOWN = '\x01';

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

// 略符号 (文字間を空けずに続けて送る符号)。表示は "[SK]" のような文字列
struct ProsignEntry {
  const char* code;
  const char* text;
};

inline const ProsignEntry* prosignTable(int& count) {
  static const ProsignEntry table[] = {
      {"...-.-", "[SK]"},
  };
  count = sizeof(table) / sizeof(table[0]);
  return table;
}

// 符号 → 表示文字列 (1 文字または略符号)。未対応なら nullptr
inline const char* morseDecodeText(const char* code) {
  int n;
  const ProsignEntry* p = prosignTable(n);
  for (int i = 0; i < n; i++)
    if (strcmp(p[i].code, code) == 0) return p[i].text;
  static char one[2];
  const MorseEntry* t = morseTable(n);
  for (int i = 0; i < n; i++) {
    if (strcmp(t[i].code, code) == 0) {
      one[0] = t[i].ch;
      one[1] = 0;
      return one;
    }
  }
  return nullptr;
}

}  // namespace cw
