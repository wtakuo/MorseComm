// build_info.h - Build date/time (shown on the splash screen and by show)
//
// arduino-cli does not recompile unchanged sources, so __DATE__ / __TIME__ in a
// single file would stay stale when only other files are modified. Instead,
// each source (.cpp / .ino) has BUILD_INFO_UNIT(), which registers its compile
// date/time at startup, and the latest one is used as the build date/time
// (modified files are always recompiled). When adding a source file, also add
// BUILD_INFO_UNIT().
#pragma once

namespace buildinfo {

void note(const char* date,
          const char* time);  // __DATE__ ("Nov  1 2026"), __TIME__ ("12:34:22")
const char* stamp();          // Latest compile date/time "20261101123422"
// git commit hash (7 digits, with a trailing "+" if there are uncommitted
// changes). Written to git_version.h by tools/update-git-version.sh before the
// build. "unknown" if git_version.h does not exist
const char* commit();

struct Unit {
    Unit(const char* date, const char* time) { note(date, time); }
};

}  // namespace buildinfo

#define BUILD_INFO_UNIT() \
    static buildinfo::Unit buildInfoUnit_(__DATE__, __TIME__)
