// morse_table.h - international Morse code table (Arduino-independent)
#pragma once
#include <string.h>

namespace cw {

// Character representing an undecodable code (shown as a red '?' on the
// display). Distinguishes it from a correctly received '?'
constexpr char CHAR_UNKNOWN = '\x01';

struct MorseEntry {
    char ch;
    const char* code;
};

inline const MorseEntry* morseTable(int& count) {
    static const MorseEntry table[] = {
        {'A', ".-"},     {'B', "-..."},    {'C', "-.-."},   {'D', "-.."},
        {'E', "."},      {'F', "..-."},    {'G', "--."},    {'H', "...."},
        {'I', ".."},     {'J', ".---"},    {'K', "-.-"},    {'L', ".-.."},
        {'M', "--"},     {'N', "-."},      {'O', "---"},    {'P', ".--."},
        {'Q', "--.-"},   {'R', ".-."},     {'S', "..."},    {'T', "-"},
        {'U', "..-"},    {'V', "...-"},    {'W', ".--"},    {'X', "-..-"},
        {'Y', "-.--"},   {'Z', "--.."},    {'0', "-----"},  {'1', ".----"},
        {'2', "..---"},  {'3', "...--"},   {'4', "....-"},  {'5', "....."},
        {'6', "-...."},  {'7', "--..."},   {'8', "---.."},  {'9', "----."},
        {'.', ".-.-.-"}, {',', "--..--"},  {'?', "..--.."}, {'/', "-..-."},
        {'=', "-...-"},  {'+', ".-.-."},   {'-', "-....-"}, {'(', "-.--."},
        {')', "-.--.-"}, {'\'', ".----."}, {':', "---..."}, {';', "-.-.-."},
        {'"', ".-..-."}, {'@', ".--.-."},  {'!', "-.-.--"}, {'_', "..--.-"},
        {'&', ".-..."},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

// Character -> code (sequence of "." and "-"). nullptr if unsupported
inline const char* morseEncode(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    int n;
    const MorseEntry* t = morseTable(n);
    for (int i = 0; i < n; i++)
        if (t[i].ch == c) return t[i].code;
    return nullptr;
}

// Prosigns (codes sent continuously without a character gap). Displayed as a
// string such as "[SK]"
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

// Code -> display string (one character or a prosign). nullptr if unsupported
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
