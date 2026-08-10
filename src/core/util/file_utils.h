// file_utils.h - Cross-platform, Unicode-safe file opening helpers.
//
// On Windows, std::ifstream/std::ofstream open paths through the CRT's
// ANSI code page, so UTF-8 encoded Chinese/non-ASCII paths fail (or open
// the wrong file). Qt's QFile is fine (UTF-16 internally), but the core /
// audio layers intentionally avoid a Qt dependency. This helper bridges
// the gap: it converts a UTF-8 path to UTF-16 and opens it with _wfopen,
// which handles arbitrary Unicode paths. On non-Windows platforms it
// simply falls back to fopen.
#ifndef AUDIOT_CORE_FILE_UTILS_H
#define AUDIOT_CORE_FILE_UTILS_H

#include <cstdio>
#include <string>

namespace audiot {

// Open a file given a UTF-8 encoded path. mode uses fopen semantics
// ("rb", "wb", ...). Returns nullptr on failure. Caller must fclose().
// On Windows the path is converted to UTF-16 and opened via _wfopen so
// Chinese / non-ASCII paths work; elsewhere fopen is used directly.
FILE* openFile(const std::string& utf8Path, const char* mode);

} // namespace audiot
#endif // AUDIOT_CORE_FILE_UTILS_H
