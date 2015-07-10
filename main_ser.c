/* 
 * File:   main.c
 * Author: dominik
 *
 * Created on 6 lipiec 2015, 11:43
 */

#include <stdlib.h>
#include <stdio.h>
#include <uv.h>
#include <assert.h>
#include <string.h>

#define DEFAULT_ADDRESS INADDR_ANY
#define DEFAULT_PORT 1500
#define DEFAULT_BACKLOG 10
#define NOIPC 0

#define CHECK(r, msg) if (r) {                                                          \
    fprintf(stderr, "%s: [%s(%d): %s]\n", msg, uv_err_name((r)), r, uv_strerror((r))); \
    exit(EXIT_FAILURE);                                                                 \
}

typedef struct context_struct {
    uv_fs_t *open_req;
    uv_fs_t *read_req;
} context_t;

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf);
void connection(uv_stream_t* server, int status);
void read_file_name(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
void open_file(uv_fs_t*);
void read_file(uv_fs_t*);
void send_file(uv_fs_t*);
void close_file(uv_fs_t*);

char* address;
int port;
int backlog;
int r;

int main(int argc, char* argv[]) {
    if (argv[1] != "0") address = argv[1];
    else address = DEFAULT_ADDRESS;
    if (argv[2] != "0") port = atoi(argv[2]);
    else port = DEFAULT_PORT;
    if (argv[3] != "0") backlog = atoi(argv[3]);
    else backlog = DEFAULT_BACKLOG;

    uv_loop_t* loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
    r = uv_loop_init(loop);
    CHECK(r, "Loop init");
    
    uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    r = uv_tcp_init(loop, server);
    CHECK(r, "TCP init");

    struct sockaddr_in addr;
    uv_ip4_addr(address, port, &addr);
    fprintf(stderr, "settings serwer: %s %d %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), backlog);

    r = uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);
    CHECK(r, "Bind");

    r = uv_listen((uv_stream_t*)server, backlog, connection);
    CHECK(r, "Listen");

    r = uv_run(loop, UV_RUN_DEFAULT);
    CHECK(r, "Run");
    
    r = uv_loop_close(loop);
    CHECK(r, "Loop close");
    
    free(server);
    free(loop);
    return EXIT_SUCCESS;
}

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init((char*)malloc(size), size);
}

void connection(uv_stream_t* server, int status) {
    CHECK(status, "New connection");
    
    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(server->loop, client);

    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_buffer, read_file_name);
    } else {
        uv_close((uv_handle_t*) client, NULL);
    }
}

void read_file_name(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    if (nread == UV_EOF) {
        uv_close((uv_handle_t*)client, NULL);
    } else if (nread > 0) {
        fprintf(stdout, "###\tread %ld bytes: %s\n", nread, buf->base);

        if (!strcmp(buf->base, "KILL")) uv_stop(client->loop);

        uv_fs_t* open_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
        context_t* context = malloc(sizeof(context_t));
        context->open_req  = open_req;
        open_req->data = context;
        
        uv_fs_t* access_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
        if(uv_fs_access(client->loop, access_req, buf->base, O_RDONLY, NULL) == 0) {
            r = uv_fs_open(client->loop, open_req, buf->base, O_RDONLY, S_IRUSR, open_file);
            CHECK(r, "uv_fs_open");
        }
    }
    if (nread == 0) free(buf->base);
}

void open_file(uv_fs_t* open_req) {
    if (open_req->result < 0) CHECK((int)open_req->result, "uv_fs_open callback");
    
    uv_fs_t *stat_req = malloc(sizeof(uv_fs_t));
    r = uv_fs_stat(open_req->loop, stat_req, open_req->path, NULL);
    CHECK(r, "uv_fs_stat");
    
    uv_buf_t* buf = (uv_buf_t*)malloc(sizeof(uv_buf_t));
    *buf = uv_buf_init((char*)malloc(stat_req->statbuf.st_size), stat_req->statbuf.st_size);
    
    uv_fs_t *read_req = malloc(sizeof(uv_fs_t));
    context_t* context = open_req->data;
    context->read_req = read_req;
    read_req->data = context;
    
    r = uv_fs_read(open_req->loop, read_req, open_req->result, buf, 1, 0, read_file);
    CHECK(r, "uv_fs_read");
}

void read_file(uv_fs_t* read_req) {
    if (read_req->result < 0) CHECK((int)read_req->result, "uv_fs_read callback");
    
    fprintf(stdout, "\tbufs->base:\n%s\n\tbufs->len: %ld\n", read_req->bufs->base, read_req->bufs->len);

    uv_fs_t *sendfile_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
    //r = uv_fs_sendfile(read_req->loop, sendfiel_req, uv_file out_fd, uv_file in_fd, int64_t in_offset, read->bufs->len, NULL);
    fprintf(stdout, "test\n");
    fprintf(stdout, "active handles: %s", read_req->bufs->base);
    //r = uv_write(sendfile_req, read_req->loop->active_handles, read_req->bufs, 1, NULL);
    CHECK(r, "uv_fs_sendfile");    
    
    free(read_req->bufs->base);
    
    uv_fs_t *close_req = malloc(sizeof(uv_fs_t));
    context_t* context = read_req->data;
    close_req->data = context;

    r = uv_fs_close(read_req->loop, close_req, context->open_req->result, close_file);
    CHECK(r, "uv_fs_close");
}

void send_file(uv_fs_t* sendfile_req) {
    if (sendfile_req->result < 0) CHECK((int)sendfile_req->result, "uv_fs_sendfile callback");
    
}

void close_file(uv_fs_t* close_req) {
    if (close_req->result < 0) CHECK((int)close_req->result, "uv_fs_close callback");

    context_t* context = close_req->data;
    
    uv_fs_req_cleanup(context->open_req);
    uv_fs_req_cleanup(context->read_req);
    uv_fs_req_cleanup(close_req);
    free(context);
}