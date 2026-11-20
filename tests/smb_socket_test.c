#include "ps5_smb_socket.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int fd[2];
    char a[4] = {0}, b[4] = {0};
    struct iovec vectors[] = {{a, 3}, {NULL, 0}, {b, 3}};
    alarm(5);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    assert(!(fcntl(fd[0], F_GETFL) & O_NONBLOCK));
    errno = 0;
    assert(ps5_smb_socket_readv(fd[0], vectors, 3) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    puts("PASS idle blocking socket returns EAGAIN");

    assert(write(fd[1], "abc", 3) == 3);
    assert(ps5_smb_socket_readv(fd[0], vectors, 3) == 3);
    assert(!memcmp(a, "abc", 3) && !b[0]);
    puts("PASS partial vector progress returned before second vector wait");

    assert(write(fd[1], "abcdef", 6) == 6);
    assert(ps5_smb_socket_readv(fd[0], vectors, 3) == 6);
    assert(!memcmp(a, "abc", 3) && !memcmp(b, "def", 3));
    assert(ps5_smb_socket_writev(fd[0], vectors, 3) == 6);
    char received[6];
    assert(read(fd[1], received, 6) == 6);
    assert(!memcmp(received, "abcdef", 6));
    puts("PASS ordered multi-vector read/write and empty vector");

    int small = 4096;
    assert(setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    char block[8192] = {0};
    while (send(fd[0], block, sizeof(block), MSG_DONTWAIT) > 0) {}
    errno = 0;
    assert(ps5_smb_socket_writev(fd[0], vectors, 3) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    puts("PASS full send buffer returns EAGAIN");
    assert(!(fcntl(fd[0], F_GETFL) & O_NONBLOCK));
    puts("PASS descriptor flags unchanged");

    assert(shutdown(fd[1], SHUT_WR) == 0);
    assert(ps5_smb_socket_readv(fd[0], vectors, 3) == 0);
    puts("PASS peer EOF preserved");
    close(fd[0]);
    close(fd[1]);
    return 0;
}
