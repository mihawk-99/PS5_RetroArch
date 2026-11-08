/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
/* The scraper's HTTP client (src/scraper.h).
 *
 * On the console, Sony's classic HTTP library (HTTP/1.1, which every media host
 * speaks; its HTTP/2 library failed every request to thumbnails.libretro.com) over its
 * SSL: one library context for the process, one template per worker, one kept-alive
 * connection a host, so each worker reuses its connection across thousands of small
 * requests instead of connecting for each. The platform's certificate and hostname
 * checks stay on.
 *
 * On the host (the tests), plain http:// over a socket, enough for the fake servers
 * tests/test_scraper.py plays. */
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ps5_scraper
{
struct Response
{
    int status = 0;     /* the HTTP status, 0 when none arrived */
    std::string body;   /* up to the limit, when no file was given */
    uint64_t bytes = 0; /* the body's length */
    bool cancelled = false, too_large = false;
    std::string error; /* why there is no status */
};

class Http
{
  public:
    explicit Http(const char *agent);
    ~Http();
    Http(const Http &) = delete;
    Http &operator=(const Http &) = delete;
    /* GET url: the body kept (at most limit bytes), or written to fd when fd >= 0.
     * cancel, when set, stops the transfer between reads. */
    Response get(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel,
                 int fd = -1, std::function<void(uint64_t)> progress = {});

  private:
    Response request(const std::string &url, uint64_t limit, const std::atomic<bool> *cancel,
                     int fd, const std::function<void(uint64_t)> &progress);
    struct State;
    std::unique_ptr<State> state_;
};

/* The certificates HTTPS is verified against (the daemon's curl build; a no-op where
 * the platform's own store is used). */
void set_certificates(const std::string &path);
/* %-encodes everything but unreserved characters (RFC 3986). */
std::string url_encode(const std::string &text);
/* Writes a URL with its query's credentials masked, for logs and status. */
std::string redact(const std::string &url);
} // namespace ps5_scraper
