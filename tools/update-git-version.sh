#!/bin/sh
# git のコミットハッシュを git_version.h に書く (スプラッシュスクリーンと show に出す)。ビルドの前に実行する。
# 内容が変わったときだけ書き換える (変わらなければ再コンパイルを起こさない)。
# コミットしていない変更があるときは、ハッシュの後ろに "+" を付ける。
cd "$(git rev-parse --show-toplevel)" || exit 0
hash=$(git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
git diff --quiet HEAD -- . ':!git_version.h' 2>/dev/null || hash="$hash+"
new="// 自動生成 (tools/update-git-version.sh)。編集しないこと
#define GIT_COMMIT \"$hash\""
[ -f git_version.h ] && [ "$(cat git_version.h)" = "$new" ] && exit 0
printf '%s\n' "$new" > git_version.h
