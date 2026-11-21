#ifndef PS5_ESDE_SMB_MEDIA_H
#define PS5_ESDE_SMB_MEDIA_H
#include <memory>
#include <string>
#include <cstddef>
struct AVFormatContext;
struct AVInputFormat;
struct AVDictionary;
namespace SmbMedia {
std::shared_ptr<unsigned char> readImage(const std::string& path, size_t& size);
int openVideo(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**);
void closeVideo(AVFormatContext**);
}
#endif
