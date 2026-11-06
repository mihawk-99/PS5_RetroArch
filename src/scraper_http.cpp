/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
/* The scraper's HTTP client (src/scraper_http.h). */
#include "scraper_http.h"

#if defined(PS5_SCRAPER_CURL)
#include <curl/curl.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <vector>

#if defined(__PROSPERO__) && !defined(PS5_SCRAPER_CURL)
extern "C"
{
    int sceNetInit();
    int sceNetPoolCreate(const char *, int, int);
    int sceSslInit(size_t);
    int sceHttpInit(int, int, size_t);
    int sceHttpCreateTemplate(int, const char *, int, int);
    int sceHttpDeleteTemplate(int);
    int sceHttpCreateConnectionWithURL(int, const char *, int);
    int sceHttpDeleteConnection(int);
    int sceHttpCreateRequestWithURL(int, int, const char *, uint64_t);
    int sceHttpDeleteRequest(int);
    int sceHttpSendRequest(int, const void *, size_t);
    int sceHttpGetStatusCode(int, int *);
    int sceHttpReadData(int, void *, size_t);
    int sceHttpSetAutoRedirect(int, int);
    int sceHttpSetConnectTimeOut(int, unsigned);
    int sceHttpSetRecvTimeOut(int, unsigned);
}
#include <map>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace ps5_scraper
{
namespace
{
bool write_all(int fd, const char *data, size_t size)
{
    while (size)
    {
        const ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        data += n;
        size -= size_t(n);
    }
    return true;
}
/* Takes a piece of the body: kept, or written to fd. */
bool take(Response &response, const char *data, size_t size, uint64_t limit, int fd)
{
    if (response.bytes + size > limit)
    {
        response.too_large = true;
        return false;
    }
    response.bytes += size;
    if (fd >= 0)
        return write_all(fd, data, size);
    response.body.append(data, size);
    return true;
}
#if defined(__PROSPERO__) && !defined(PS5_SCRAPER_CURL)
/* One library context for the process: the net pool, SSL and HTTP, never torn down.
 * Sony's classic HTTP library (HTTP/1.1, keep-alive), not its HTTP/2 one: media hosts
 * such as thumbnails.libretro.com answer HTTP/1.1 only. */
int library_context()
{
    static std::once_flag once;
    static int http = -1;
    std::call_once(once,
                   []
                   {
                       (void)sceNetInit();
                       const int pool = sceNetPoolCreate("PS5 RetroArch scraper", 4 << 20, 0);
                       const int ssl = pool >= 0 ? sceSslInit(2 << 20) : -1;
                       http = ssl >= 0 ? sceHttpInit(pool, ssl, 4 << 20) : -1;
                   });
    return http;
}
std::string code(const char *call, int result)
{
    char text[64];
    std::snprintf(text, sizeof text, "%s failed (0x%08x).", call, unsigned(result));
    return text;
}
/* scheme://host[:port], the key of a kept-alive connection. */
std::string origin(const std::string &url)
{
    const size_t start = url.find("://");
    const size_t end = start == std::string::npos ? url.size() : url.find('/', start + 3);
    return url.substr(0, end);
}
#endif
} // namespace

#if defined(PS5_SCRAPER_CURL)
/* libcurl over mbedTLS (tools/build-curl.sh): the daemon's HTTP and HTTPS. Sony's SSL
 * failed every handshake from the payload (sceHttpSendRequest 0x8095f00c, 2026-10-07);
 * curl verifies against the bundled certificates (set_certificates). One easy handle a
 * worker: its connections are kept alive across requests to the same host. */
namespace
{
std::string certificates;
struct Sink
{
    Response *response;
    uint64_t limit;
    int fd;
    const std::atomic<bool> *cancel;
    const std::function<void(uint64_t)> *progress;
};
size_t on_data(char *data, size_t size, size_t count, void *context)
{
    auto *sink = static_cast<Sink *>(context);
    const size_t bytes = size * count;
    if (sink->cancel && sink->cancel->load())
    {
        sink->response->cancelled = true;
        return 0;
    }
    if (!take(*sink->response, data, bytes, sink->limit, sink->fd))
        return 0;
    if (*sink->progress)
        (*sink->progress)(sink->response->bytes);
    return bytes;
}
int on_progress(void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto *sink = static_cast<Sink *>(context);
    if (sink->cancel && sink->cancel->load())
    {
        sink->response->cancelled = true;
        return 1;
    }
    return 0;
}
} // namespace

void set_certificates(const std::string &path)
{
    certificates = path;
}

struct Http::State
{
    std::string agent;
    CURL *handle = nullptr;
};

Http::Http(const char *agent) : state_(new State)
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    state_->agent = agent;
    state_->handle = curl_easy_init();
}

Http::~Http()
{
    if (state_->handle)
        curl_easy_cleanup(state_->handle);
}

Response Http::get(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel, int fd,
                   std::function<void(uint64_t)> progress)
{
    return request(url, limit, cancel, fd, progress);
}

Response Http::request(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel,
                       int fd, const std::function<void(uint64_t)> &progress)
{
    Response response;
    CURL *curl = state_->handle;
    if (!curl)
    {
        response.error = "The HTTP library did not start.";
        return response;
    }
    Sink sink{&response, limit, fd, cancel, &progress};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, state_->agent.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    /* An https request is never redirected to http: its address may carry an account. */
    if (url.rfind("https://", 0) == 0)
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    if (!certificates.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificates.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = int(status);
    if (result != CURLE_OK && !response.cancelled && !response.too_large)
    {
        char text[160];
        std::snprintf(text, sizeof text, "%s (curl %d).", curl_easy_strerror(result), int(result));
        response.error = text;
        if (result != CURLE_WRITE_ERROR)
            response.status = 0; /* no answer we can use */
    }
    return response;
}

#else
void set_certificates(const std::string &)
{
}
struct Http::State
{
    std::string agent;
#ifdef __PROSPERO__
    int templ = -1;
    std::map<std::string, int> connections; /* origin -> kept-alive connection */
#endif
};

Http::Http(const char *agent) : state_(new State)
{
    state_->agent = agent;
#ifdef __PROSPERO__
    const int http = library_context();
    if (http >= 0)
    {
        state_->templ = sceHttpCreateTemplate(http, agent, 2 /* HTTP/1.1 */, 1);
        if (state_->templ >= 0)
        {
            sceHttpSetAutoRedirect(state_->templ, 1);
            sceHttpSetConnectTimeOut(state_->templ, 15000000);
            sceHttpSetRecvTimeOut(state_->templ, 30000000);
        }
    }
#endif
}

Http::~Http()
{
#ifdef __PROSPERO__
    for (const auto &connection : state_->connections)
        sceHttpDeleteConnection(connection.second);
    if (state_->templ >= 0)
        sceHttpDeleteTemplate(state_->templ);
#endif
}

Response Http::get(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel, int fd,
                   std::function<void(uint64_t)> progress)
{
    return request(url, limit, cancel, fd, progress);
}

Response Http::request(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel,
                       int fd, const std::function<void(uint64_t)> &progress)
{
    Response response;
#ifdef __PROSPERO__
    if (state_->templ < 0)
    {
        response.error = "The console's network library did not start.";
        return response;
    }
    // One kept-alive connection a host a worker; a request that fails on it is tried
    // once more on a fresh one (the server may have closed it meanwhile).
    const std::string host = origin(url);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        int &connection = state_->connections[host];
        if (connection <= 0)
            connection = sceHttpCreateConnectionWithURL(state_->templ, url.c_str(), 1);
        if (connection < 0)
        {
            response.error = code("sceHttpCreateConnectionWithURL", connection);
            state_->connections.erase(host);
            return response;
        }
        const int request = sceHttpCreateRequestWithURL(connection, 0 /* GET */, url.c_str(), 0);
        if (request < 0)
        {
            response.error = code("sceHttpCreateRequestWithURL", request);
            return response;
        }
        int result = sceHttpSendRequest(request, nullptr, 0);
        if (result >= 0)
            result = sceHttpGetStatusCode(request, &response.status);
        if (result < 0)
        {
            response.status = 0;
            response.error = code("sceHttpSendRequest", result);
            sceHttpDeleteRequest(request);
            sceHttpDeleteConnection(connection);
            state_->connections.erase(host);
            continue;
        }
        // On the heap: a payload's threads have small stacks (a 64 KiB frame overflowed
        // one, 2026-10-07: SIGSEGV writing just below the stack).
        std::vector<char> buffer(65536);
        int n = 0;
        while ((n = sceHttpReadData(request, buffer.data(), buffer.size())) > 0)
        {
            if (cancel && cancel->load())
            {
                response.cancelled = true;
                break;
            }
            if (!take(response, buffer.data(), size_t(n), limit, fd))
                break;
            if (progress)
                progress(response.bytes);
        }
        if (n < 0 && !response.cancelled && !response.too_large)
            response.error = code("sceHttpReadData", n);
        sceHttpDeleteRequest(request);
        if (response.cancelled || response.too_large || n < 0)
        {
            /* Unread data left on it: the connection is not reused. */
            sceHttpDeleteConnection(connection);
            state_->connections.erase(host);
        }
        return response;
    }
    return response;
#else
    /* http://host[:port]/path, for the tests' fake servers. */
    if (url.rfind("http://", 0) != 0)
    {
        response.error = "Only http:// on the host.";
        return response;
    }
    const size_t host_start = 7, path_start = url.find('/', host_start);
    std::string host = url.substr(host_start, path_start - host_start), port = "80";
    const std::string path = path_start == std::string::npos ? "/" : url.substr(path_start);
    if (const size_t colon = host.find(':'); colon != std::string::npos)
    {
        port = host.substr(colon + 1);
        host.resize(colon);
    }
    addrinfo hints{}, *found = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) || !found)
    {
        response.error = "The server's name did not resolve.";
        return response;
    }
    const int sock = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    timeval timeout{30, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const bool connected = sock >= 0 && connect(sock, found->ai_addr, found->ai_addrlen) == 0;
    freeaddrinfo(found);
    if (!connected)
    {
        if (sock >= 0)
            close(sock);
        response.error = "The server did not answer (network or DNS).";
        return response;
    }
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host +
                                "\r\nUser-Agent: " + state_->agent + "\r\n\r\n";
    write_all(sock, request.data(), request.size());
    std::string head;
    std::vector<char> storage(65536);
    char *buffer = storage.data();
    bool in_body = false;
    ssize_t n;
    while ((n = read(sock, buffer, storage.size())) > 0)
    {
        if (cancel && cancel->load())
        {
            response.cancelled = true;
            break;
        }
        if (in_body)
        {
            if (!take(response, buffer, size_t(n), limit, fd))
                break;
            continue;
        }
        head.append(buffer, size_t(n));
        const size_t end = head.find("\r\n\r\n");
        if (end == std::string::npos)
            continue;
        response.status = std::atoi(head.c_str() + head.find(' ') + 1);
        in_body = true;
        if (!take(response, head.data() + end + 4, head.size() - end - 4, limit, fd))
            break;
    }
    close(sock);
    if (!in_body && response.error.empty())
        response.error = "The server's answer was cut short.";
    return response;
#endif
}

#endif

std::string url_encode(const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string redact(const std::string &url)
{
    std::string out = url;
    for (const char *key : {"devpassword=", "sspassword=", "devid=", "ssid="})
    {
        size_t at = 0;
        while ((at = out.find(key, at)) != std::string::npos)
        {
            if (at > 0 && out[at - 1] != '?' && out[at - 1] != '&')
            {
                at += std::strlen(key);
                continue;
            }
            const size_t start = at + std::strlen(key), end = out.find('&', start);
            out.replace(start, (end == std::string::npos ? out.size() : end) - start, "***");
            at = start + 3;
        }
    }
    return out;
}
} // namespace ps5_scraper
