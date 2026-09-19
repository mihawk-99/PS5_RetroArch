/* PS5 RetroArch - public SDK directory enumeration adapter. */
#pragma once
#include <dirent.h>
#ifdef __cplusplus
extern "C"
{
#endif
    DIR *ps5_opendir(const char *path);
    struct dirent *ps5_readdir(DIR *directory);
    int ps5_closedir(DIR *directory);
#ifdef __cplusplus
}
#endif
