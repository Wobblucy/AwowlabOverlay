#include "ExecutablePath.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

namespace awow {

std::string getExecutableDirectory() {
#if defined(_WIN32) || defined(_WIN64)
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string fullPath(path);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(0, lastSlash + 1);
    }
    return "";
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count != -1) {
        std::string fullPath(path, count);
        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            return fullPath.substr(0, lastSlash + 1);
        }
    }
    return "";
#endif
}

} // namespace awow
