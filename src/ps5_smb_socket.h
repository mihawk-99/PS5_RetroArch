/* Socket-only I/O for libsmb2; not a replacement for general readv/writev. */
#ifndef PS5_SMB_SOCKET_H
#define PS5_SMB_SOCKET_H

#include <sys/types.h>
#include <sys/uio.h>

ssize_t ps5_smb_socket_readv(int fd, const struct iovec *vectors, int count);
ssize_t ps5_smb_socket_writev(int fd, const struct iovec *vectors, int count);

#endif
