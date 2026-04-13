// ============================
// GraceOS Network Stack
// http_client.h - HTTP/1.1 Client
// ============================

#ifndef GRACEOS_HTTP_CLIENT_H
#define GRACEOS_HTTP_CLIENT_H

#include "../../../lib/libc/int.h"

// ============================
// HTTP Client API
// ============================

// Perform an HTTP/1.1 GET request.
// host and path are NUL-terminated ASCII strings.
// Response body is written into buffer up to max_size bytes.
// Returns number of body bytes written, or -1 on error.
int http_fetch(const char *host, const char *path,
               void *buffer, size_t max_size);

// Streaming HTTP/1.1 GET.
// The write_cb is called repeatedly with chunks of the body.
// Returns total bytes delivered to write_cb, or -1 on error.
int http_fetch_stream(const char *host, const char *path,
                      int (*write_cb)(const void *data, size_t len));

#endif // GRACEOS_HTTP_CLIENT_H
