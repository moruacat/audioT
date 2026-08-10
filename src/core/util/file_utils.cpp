// file_utils.cpp - Cross-platform, Unicode-safe file opening helpers.
#include "file_utils.h"

#ifdef _WIN32
#include <windows.h>
#include <vector>
#endif

namespace audiot {

FILE* openFile(const std::string& utf8Path, const char* mode) {
#ifdef _WIN32
    // Convert UTF-8 path -> UTF-16 (wide) so Chinese/non-ASCII paths work.
    const int pathLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (pathLen <= 0) {
        return nullptr;
    }
    std::vector<wchar_t> wpath(static_cast<size_t>(pathLen));
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, wpath.data(), pathLen);

    // mode is ASCII; convert to wide (no MBCS concerns).
    wchar_t wmode[8] = { 0 };
    for (int i = 0; mode[i] != '\0' && i < 7; ++i) {
        wmode[i] = static_cast<wchar_t>(mode[i]);
    }

    return _wfopen(wpath.data(), wmode);
#else
    return std::fopen(utf8Path.c_str(), mode);
#endif
}

} // namespace audiot
