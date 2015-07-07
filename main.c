#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <uv.h>

#define DEFAULT_ADDRESS "127.0.0.1"
#define DEFAULT_PORT 1500
#define DEFAULT_BACKLOG 10

#define ERROR(msg, code) do {                                                         \
  fprintf(stderr, "%s: [%s: %s]\n", msg, uv_err_name((code)), uv_strerror((code)));   \
  assert(0);                                                                          \
} while(0);

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

void connection(uv_stream_t *server, int status);
void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf);
void read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
void write_cb(uv_write_t *req, int status);

uv_loop_t *loop;

int main() {
    loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in addr;
    uv_ip4_addr(DEFAULT_ADDRESS, DEFAULT_PORT, &addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*) &server, DEFAULT_BACKLOG, connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}

void connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        // error!
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    
    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_read_start((uv_stream_t*) client, alloc_buffer, read_cb);
    }
    else {
        uv_close((uv_handle_t*) client, NULL);
    }
}

void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
    *buf = uv_buf_init((char*) malloc(size), size);
}

void read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread == UV_EOF) {
        uv_close((uv_handle_t*) client, NULL);
    } else if (nread > 0) {
        fprintf(stderr, "%ld bytes read\n", nread);
        
        write_req_t *wr = (write_req_t*) malloc(sizeof(write_req_t));
        wr->buf =  uv_buf_init(buf->base, nread);
        uv_write(&wr->req, client, &wr->buf, 1/*nbufs*/, write_cb);
    }
    if (nread == 0) free(buf->base);
}

void write_cb(uv_write_t *req, int status) {
    write_req_t* wr = (write_req_t*) req;
    
    int written = wr->buf.len;
    if (status) ERROR("async write", status);
    assert(wr->req.type == UV_WRITE);
    fprintf(stderr, "%d bytes written\n", written);
    
    free(wr->buf.base);
    free(wr);
}
