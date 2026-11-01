#include "build_info.h"
#if __has_include("git_version.h")
#include "git_version.h"  // tools/update-git-version.sh が生成する (git には入れない)
#endif
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

BUILD_INFO_UNIT();

namespace buildinfo {
namespace {

// 静的初期化の順序に依存しないよう、関数内 static に置く
uint64_t& latest() {
  static uint64_t v = 0;
  return v;
}

}  // namespace

void note(const char* date, const char* time) {
  static const char* const MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {date[0], date[1], date[2], 0};
  const char* p = strstr(MONTHS, mon);
  int month = p ? (int)(p - MONTHS) / 3 + 1 : 0;
  int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + (date[5] - '0');
  int year = 0;
  for (int i = 7; i < 11; i++) year = year * 10 + (date[i] - '0');
  int hh = (time[0] - '0') * 10 + (time[1] - '0');
  int mm = (time[3] - '0') * 10 + (time[4] - '0');
  int ss = (time[6] - '0') * 10 + (time[7] - '0');
  uint64_t v = ((((uint64_t)year * 100 + month) * 100 + day) * 100 + hh) * 10000 + mm * 100 + ss;
  if (v > latest()) latest() = v;
}

const char* commit() { return GIT_COMMIT; }

const char* stamp() {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)latest());
  return buf;
}

}  // namespace buildinfo
