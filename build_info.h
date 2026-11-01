// build_info.h — ビルド日時 (スプラッシュスクリーンと show に出す)
//
// arduino-cli は変わっていないソースを再コンパイルしないので、1 つのファイルの __DATE__ / __TIME__ では
// 他のファイルだけを直したときに古い日時のままになる。そこで、各ソース (.cpp / .ino) に BUILD_INFO_UNIT() を書いて
// それぞれのコンパイル日時を起動時に登録し、その最新をビルド日時とする (変更したファイルは必ずコンパイルし直される)。
// ソースファイルを追加したら BUILD_INFO_UNIT() も書くこと。
#pragma once

namespace buildinfo {

void note(const char* date, const char* time);  // __DATE__ ("Nov  1 2026"), __TIME__ ("12:34:22")
const char* stamp();                             // 最新のコンパイル日時 "20261101123422"
// git のコミットハッシュ (7 桁。未コミットの変更があれば末尾に "+")。ビルド前に tools/update-git-version.sh が
// git_version.h に書く。git_version.h が無ければ "unknown"
const char* commit();

struct Unit {
  Unit(const char* date, const char* time) { note(date, time); }
};

}  // namespace buildinfo

#define BUILD_INFO_UNIT() static buildinfo::Unit buildInfoUnit_(__DATE__, __TIME__)
