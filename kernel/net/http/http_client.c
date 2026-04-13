// ============================
// GraceOS Network Stack
// http_client.c - HTTP/1.1 GET Client
// ============================

#include "http_client.h"
#include "../tcp/tcp.h"
#include "../net.h"
#include "../arp/arp.h"
#include "../ethernet/ethernet.h"
#include "../../../lib/libc/string.h"
#include "../../../kernel/log/klog.h"

// ============================
// DNS resolution
// (Minimal: only numeric IPs supported for now)
// ============================

#define HTTP_REQ_BUF_SIZE    512
#define HTTP_HEADER_BUF_SIZE 2048

// Delivery results for sink callbacks
#define HTTP_SINK_OK   0
#define HTTP_SINK_FULL 1
#define HTTP_SINK_ERR -1

// Parse a dotted-decimal IPv4 string into host byte order.
// Returns 0 on failure.
static uint32_t parse_ipv4(const char *s)
{
    uint32_t result = 0;
    int octet = 0;
    int dots = 0;
    int have_digit = 0;

    if (!s || !*s)
        return 0;

    while (*s)
    {
        if (*s >= '0' && *s <= '9')
        {
            have_digit = 1;
            octet = octet * 10 + (*s - '0');
            if (octet > 255)
                return 0;
        }
        else if (*s == '.')
        {
            if (!have_digit || dots >= 3)
                return 0;
            result = (result << 8) | (uint32_t)octet;
            dots++;
            octet = 0;
            have_digit = 0;
        }
        else
        {
            return 0;
        }
        s++;
    }

    if (!have_digit || dots != 3)
        return 0;

    result = (result << 8) | (uint32_t)octet;
    return result;
}

static uint32_t make_ipv4(int a, int b, int c, int d)
{
    return ((uint32_t)a << 24) |
           ((uint32_t)b << 16) |
           ((uint32_t)c << 8)  |
           (uint32_t)d;
}

static uint32_t resolve_host_ipv4(const char *host)
{
    uint32_t ip = parse_ipv4(host);
    if (ip != 0)
        return ip;

    if (strcmp(host, "google.com") == 0 || strcmp(host, "www.google.com") == 0)
        return make_ipv4(142, 250, 72, 14);

    if (strcmp(host, "huggingface.co") == 0 || strcmp(host, "www.huggingface.co") == 0)
        return make_ipv4(3, 170, 185, 25);

    return 0;
}

// ============================
// HTTP/1.1 request builder
// ============================

static char http_req_buf[HTTP_REQ_BUF_SIZE];

// Simple strcat into a fixed buffer; returns new length or -1 on overflow
static int buf_cat(char *buf, size_t buf_size, size_t *pos, const char *str)
{
    while (*str)
    {
        if (*pos >= buf_size - 1)
            return -1;
        buf[(*pos)++] = *str++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int build_request(const char *host, const char *path,
                         char *buf, size_t buf_size)
{
    size_t pos = 0;
    buf[0] = '\0';

    if (buf_cat(buf, buf_size, &pos, "GET "))
        return -1;
    if (buf_cat(buf, buf_size, &pos, path))
        return -1;
    if (buf_cat(buf, buf_size, &pos, " HTTP/1.1\r\n"))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "Host: "))
        return -1;
    if (buf_cat(buf, buf_size, &pos, host))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "\r\n"))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "User-Agent: GraceOS-http/1\r\n"))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "Accept: */*\r\n"))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "Connection: close\r\n"))
        return -1;
    if (buf_cat(buf, buf_size, &pos, "\r\n"))
        return -1;

    return (int)pos;
}

// ============================
// Header parsing helpers
// ============================

typedef struct http_meta
{
    int status_code;
    int is_chunked;
    int has_content_length;
    size_t content_length;
} http_meta_t;

static char ascii_tolower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

static int ci_equal_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i]))
            return 0;
    }
    return 1;
}

static int parse_dec_u32(const char *s, size_t len, size_t *out)
{
    size_t v = 0;
    int have_digit = 0;

    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        if (c == ' ' || c == '\t')
        {
            if (!have_digit)
                continue;
            break;
        }

        if (c < '0' || c > '9')
            return -1;

        have_digit = 1;
        size_t next = v * 10 + (size_t)(c - '0');
        if (next < v)
            return -1;
        v = next;
    }

    if (!have_digit)
        return -1;

    *out = v;
    return 0;
}

static int contains_chunked_token(const char *s, size_t len)
{
    const char *needle = "chunked";
    const size_t needle_len = 7;

    if (len < needle_len)
        return 0;

    for (size_t i = 0; i + needle_len <= len; i++)
    {
        if (ci_equal_n(s + i, needle, needle_len))
            return 1;
    }
    return 0;
}

static int parse_status_code(const char *line, size_t len)
{
    // Expected shape: HTTP/1.1 200 OK
    size_t i = 0;

    if (len < 12)
        return -1;
    if (!(line[0] == 'H' && line[1] == 'T' && line[2] == 'T' && line[3] == 'P' && line[4] == '/'))
        return -1;

    while (i < len && line[i] != ' ')
        i++;
    while (i < len && line[i] == ' ')
        i++;

    if (i + 2 >= len)
        return -1;
    if (line[i] < '0' || line[i] > '9' ||
        line[i + 1] < '0' || line[i + 1] > '9' ||
        line[i + 2] < '0' || line[i + 2] > '9')
        return -1;

    return (line[i] - '0') * 100 + (line[i + 1] - '0') * 10 + (line[i + 2] - '0');
}

static int parse_headers(char *header, size_t header_len, http_meta_t *meta)
{
    // header_len includes final \r\n\r\n
    size_t pos = 0;

    memset(meta, 0, sizeof(*meta));

    // Parse status line
    size_t line_start = 0;
    while (pos + 1 < header_len)
    {
        if (header[pos] == '\r' && header[pos + 1] == '\n')
            break;
        pos++;
    }
    if (pos + 1 >= header_len)
        return -1;

    meta->status_code = parse_status_code(header + line_start, pos - line_start);
    if (meta->status_code < 100)
        return -1;

    pos += 2;

    while (pos + 1 < header_len)
    {
        line_start = pos;
        while (pos + 1 < header_len)
        {
            if (header[pos] == '\r' && header[pos + 1] == '\n')
                break;
            pos++;
        }
        if (pos + 1 >= header_len)
            return -1;

        // Empty line marks header end
        if (pos == line_start)
            break;

        // Split "name: value"
        size_t colon = line_start;
        while (colon < pos && header[colon] != ':')
            colon++;

        if (colon < pos)
        {
            const char *name = header + line_start;
            size_t name_len = colon - line_start;
            const char *value = header + colon + 1;
            size_t value_len = pos - (colon + 1);

            while (value_len > 0 && (*value == ' ' || *value == '\t'))
            {
                value++;
                value_len--;
            }

            if (name_len == 14 && ci_equal_n(name, "Content-Length", 14))
            {
                size_t cl = 0;
                if (parse_dec_u32(value, value_len, &cl) == 0)
                {
                    meta->has_content_length = 1;
                    meta->content_length = cl;
                }
            }
            else if (name_len == 17 && ci_equal_n(name, "Transfer-Encoding", 17))
            {
                if (contains_chunked_token(value, value_len))
                    meta->is_chunked = 1;
            }
        }

        pos += 2;
    }

    return 0;
}

// ============================
// ARP warm-up: wait until we can ARP-resolve the gateway/host
// ============================

#define ARP_WARMUP_RETRIES 100

static int arp_warmup(uint32_t ip)
{
    uint8_t mac[6];
    for (int i = 0; i < ARP_WARMUP_RETRIES; i++)
    {
        if (arp_lookup(ip, mac))
            return 1;
        // Drain incoming frames to capture ARP reply
        for (int j = 0; j < 64; j++)
            ethernet_poll();
    }
    return 0;
}

// ============================
// Body delivery sinks
// ============================

typedef int (*http_sink_fn)(void *ctx, const uint8_t *data, size_t len);

typedef struct http_buffer_sink_ctx
{
    uint8_t *out;
    size_t cap;
    size_t used;
} http_buffer_sink_ctx_t;

static int http_buffer_sink(void *ctx, const uint8_t *data, size_t len)
{
    http_buffer_sink_ctx_t *s = (http_buffer_sink_ctx_t *)ctx;

    if (s->used >= s->cap)
        return HTTP_SINK_FULL;

    size_t avail = s->cap - s->used;
    size_t to_copy = (len <= avail) ? len : avail;

    if (to_copy > 0)
    {
        memcpy(s->out + s->used, data, to_copy);
        s->used += to_copy;
    }

    if (to_copy < len)
        return HTTP_SINK_FULL;

    return HTTP_SINK_OK;
}

typedef struct http_stream_sink_ctx
{
    int (*write_cb)(const void *data, size_t len);
    size_t total;
} http_stream_sink_ctx_t;

static int http_stream_sink(void *ctx, const uint8_t *data, size_t len)
{
    http_stream_sink_ctx_t *s = (http_stream_sink_ctx_t *)ctx;
    if (s->write_cb(data, len) < 0)
        return HTTP_SINK_ERR;
    s->total += len;
    return HTTP_SINK_OK;
}

// ============================
// Chunked transfer decoder
// ============================

typedef enum http_chunk_state
{
    CHUNK_READ_SIZE = 0,
    CHUNK_READ_DATA,
    CHUNK_EXPECT_CR,
    CHUNK_EXPECT_LF,
    CHUNK_DONE
} http_chunk_state_t;

typedef struct http_chunk_decoder
{
    http_chunk_state_t state;
    size_t chunk_size;
    size_t chunk_read;
    char size_line[32];
    size_t size_line_len;
} http_chunk_decoder_t;

static int parse_hex_size(const char *s, size_t len, size_t *out)
{
    size_t v = 0;
    int have_digit = 0;

    for (size_t i = 0; i < len; i++)
    {
        char c = s[i];
        int d = -1;

        if (c == ';')
            break; // chunk extension

        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            d = 10 + (c - 'A');
        else if (c == ' ' || c == '\t')
        {
            if (!have_digit)
                continue;
            break;
        }
        else
        {
            return -1;
        }

        if (d >= 0)
        {
            have_digit = 1;
            size_t next = (v << 4) + (size_t)d;
            if (next < v)
                return -1;
            v = next;
        }
    }

    if (!have_digit)
        return -1;

    *out = v;
    return 0;
}

static int decode_chunked_bytes(http_chunk_decoder_t *dec,
                                const uint8_t *data,
                                size_t len,
                                http_sink_fn sink,
                                void *sink_ctx)
{
    size_t i = 0;

    while (i < len)
    {
        if (dec->state == CHUNK_DONE)
            return 1;

        if (dec->state == CHUNK_READ_SIZE)
        {
            char c = (char)data[i++];

            if (c == '\r')
                continue;

            if (c == '\n')
            {
                size_t sz = 0;
                if (parse_hex_size(dec->size_line, dec->size_line_len, &sz) < 0)
                    return -1;

                dec->chunk_size = sz;
                dec->chunk_read = 0;
                dec->size_line_len = 0;

                if (sz == 0)
                {
                    dec->state = CHUNK_DONE;
                    return 1;
                }

                dec->state = CHUNK_READ_DATA;
                continue;
            }

            if (dec->size_line_len >= sizeof(dec->size_line) - 1)
                return -1;

            dec->size_line[dec->size_line_len++] = c;
            dec->size_line[dec->size_line_len] = '\0';
            continue;
        }

        if (dec->state == CHUNK_READ_DATA)
        {
            size_t remain = dec->chunk_size - dec->chunk_read;
            size_t take = len - i;
            if (take > remain)
                take = remain;

            if (take > 0)
            {
                int sink_ret = sink(sink_ctx, data + i, take);
                if (sink_ret == HTTP_SINK_ERR)
                    return -1;
                dec->chunk_read += take;
                i += take;
                if (sink_ret == HTTP_SINK_FULL)
                    return 1;
            }

            if (dec->chunk_read == dec->chunk_size)
                dec->state = CHUNK_EXPECT_CR;
            continue;
        }

        if (dec->state == CHUNK_EXPECT_CR)
        {
            if (data[i++] != '\r')
                return -1;
            dec->state = CHUNK_EXPECT_LF;
            continue;
        }

        if (dec->state == CHUNK_EXPECT_LF)
        {
            if (data[i++] != '\n')
                return -1;
            dec->state = CHUNK_READ_SIZE;
            continue;
        }
    }

    return 0;
}

// ============================
// Shared GET implementation
// ============================

static int http_fetch_internal(const char *host,
                               const char *path,
                               http_sink_fn sink,
                               void *sink_ctx,
                               size_t *out_total)
{
    uint32_t ip = parse_ipv4(host);
    if (ip == 0)
    {
        klog_warn("http_fetch: unresolved hostname");
        return -1;
    }

    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;

    uint32_t next_hop = ((ip & dev->netmask) == (dev->ip & dev->netmask))
                      ? ip
                      : dev->gateway;

    if (!arp_warmup(next_hop))
    {
        klog_warn("http_fetch: ARP resolution failed");
        return -1;
    }

    int req_len = build_request(host, path, http_req_buf, sizeof(http_req_buf));
    if (req_len <= 0)
    {
        klog_warn("http_fetch: request too large");
        return -1;
    }

    tcp_conn_t conn;
    if (tcp_connect(&conn, ip, 80) != 0)
    {
        klog_warn("http_fetch: TCP connect failed");
        return -1;
    }

    if (tcp_send(&conn, http_req_buf, (size_t)req_len) < 0)
    {
        tcp_close(&conn);
        return -1;
    }

    uint8_t chunk[512];
    char header_buf[HTTP_HEADER_BUF_SIZE];
    size_t header_used = 0;
    int headers_done = 0;

    http_meta_t meta;
    memset(&meta, 0, sizeof(meta));

    http_chunk_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    decoder.state = CHUNK_READ_SIZE;

    size_t body_total = 0;

    while (1)
    {
        int n = tcp_recv_wait(&conn, chunk, sizeof(chunk));
        if (n <= 0)
            break;

        size_t off = 0;

        if (!headers_done)
        {
            for (int i = 0; i < n; i++)
            {
                if (header_used >= HTTP_HEADER_BUF_SIZE - 1)
                {
                    klog_warn("http_fetch: response headers too large");
                    tcp_close(&conn);
                    return -1;
                }

                header_buf[header_used++] = (char)chunk[i];
                header_buf[header_used] = '\0';

                if (header_used >= 4 &&
                    header_buf[header_used - 4] == '\r' &&
                    header_buf[header_used - 3] == '\n' &&
                    header_buf[header_used - 2] == '\r' &&
                    header_buf[header_used - 1] == '\n')
                {
                    headers_done = 1;
                    off = (size_t)i + 1;
                    break;
                }
            }

            if (!headers_done)
                continue;

            if (parse_headers(header_buf, header_used, &meta) < 0)
            {
                klog_warn("http_fetch: malformed HTTP response headers");
                tcp_close(&conn);
                return -1;
            }

            // GraceOS currently surfaces body bytes to callers regardless of
            // HTTP status class; higher layers can decide policy.
        }

        if (off >= (size_t)n)
            continue;

        const uint8_t *body = chunk + off;
        size_t body_len = (size_t)n - off;

        if (meta.is_chunked)
        {
            int ret = decode_chunked_bytes(&decoder, body, body_len, sink, sink_ctx);
            if (ret < 0)
            {
                klog_warn("http_fetch: invalid chunked response");
                tcp_close(&conn);
                return -1;
            }
            if (ret > 0)
                break;
        }
        else
        {
            size_t deliver_len = body_len;
            if (meta.has_content_length)
            {
                if (body_total >= meta.content_length)
                    break;

                size_t remain = meta.content_length - body_total;
                if (deliver_len > remain)
                    deliver_len = remain;
            }

            if (deliver_len > 0)
            {
                int sink_ret = sink(sink_ctx, body, deliver_len);
                if (sink_ret == HTTP_SINK_ERR)
                {
                    tcp_close(&conn);
                    return -1;
                }
                body_total += deliver_len;
                if (sink_ret == HTTP_SINK_FULL)
                    break;
            }

            if (meta.has_content_length && body_total >= meta.content_length)
                break;
        }
    }

    tcp_close(&conn);
    *out_total = body_total;
    return 0;
}

// ============================
// Public APIs
// ============================

int http_fetch(const char *host, const char *path,
               void *buffer, size_t max_size)
{
    if (!net_ready() || !host || !path || !buffer || max_size == 0)
        return -1;

    http_buffer_sink_ctx_t sink_ctx;
    sink_ctx.out = (uint8_t *)buffer;
    sink_ctx.cap = max_size;
    sink_ctx.used = 0;

    size_t total = 0;
    if (http_fetch_internal(host, path, http_buffer_sink, &sink_ctx, &total) < 0)
        return -1;

    (void)total;
    return (int)sink_ctx.used;
}

int http_fetch_stream(const char *host, const char *path,
                      int (*write_cb)(const void *data, size_t len))
{
    if (!net_ready() || !host || !path || !write_cb)
        return -1;

    http_stream_sink_ctx_t sink_ctx;
    sink_ctx.write_cb = write_cb;
    sink_ctx.total = 0;

    size_t total = 0;
    if (http_fetch_internal(host, path, http_stream_sink, &sink_ctx, &total) < 0)
        return -1;

    (void)total;
    return (int)sink_ctx.total;
}
