#ifndef PS5_SMB_IMAGES_H
#define PS5_SMB_IMAGES_H
#include "SmbImageCache.h"
namespace SmbImages {
inline bool remote(const std::string& path) { return path.rfind("smb://", 0) == 0; }
Cache::Ptr get(const std::string& path, bool foreground = true);
Cache::Ptr decode(const unsigned char* data, size_t size);
void window(const std::vector<std::string>& paths, size_t visible);
void beginFrame();
bool takeInstall();
bool takeUpload();
void stop();
}
#endif
