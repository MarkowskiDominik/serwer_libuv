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
#define SIZE 43776

#define CHECK(r, msg) if (r) {                                                          \
    fprintf(stderr, "%s: [%s(%d): %s]\n", msg, uv_err_name((r)), r, uv_strerror((r)));  \
    exit(EXIT_FAILURE);                                                                 \
}

typedef struct context_struct {
    uv_fs_t *open_req;
    uv_fs_t *read_req;
    uv_stream_t* tcp;
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

    uv_loop_t* loop = malloc(sizeof(uv_loop_t));
    r = uv_loop_init(loop);
    CHECK(r, "Loop init");
    
    uv_tcp_t* server = malloc(sizeof(uv_tcp_t));
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
    
    free(server);
    r = uv_loop_close(loop);
    CHECK(r, "Loop close");
    free(loop);
    
    return EXIT_SUCCESS;
}

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    *buf = uv_buf_init((char*)malloc(size), size);
}

void connection(uv_stream_t* server, int status) {
    CHECK(status, "New connection");
    
    uv_tcp_t* client = malloc(sizeof(uv_tcp_t));
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
        fprintf(stdout, "### read name: %s\n", buf->base);

        if (!strcmp(buf->base, "KILL")) uv_stop(client->loop);

        uv_fs_t* open_req = malloc(sizeof(uv_fs_t));
        context_t* context = malloc(sizeof(context_t));
        context->tcp = client;
        context->open_req = open_req;
        open_req->data = context;
        
        uv_fs_t* access_req = malloc(sizeof(uv_fs_t));
        if(uv_fs_access(client->loop, access_req, buf->base, O_RDONLY, NULL) == 0) {
            r = uv_fs_open(client->loop, open_req, buf->base, O_RDONLY, S_IRUSR, open_file);
            CHECK(r, "uv_fs_open");
        }
        else {
            uv_write_t* sendfile_req = malloc(sizeof(uv_write_t));
            uv_buf_t buf = uv_buf_init((char*)malloc(1), 1);
            r = uv_write(sendfile_req, client, &buf, 1, NULL);
            CHECK(r, "uv_fs_write");
        }
    }
    if (nread == 0) free(buf->base);
}

void open_file(uv_fs_t* open_req) {
    if (open_req->result < 0) CHECK((int)open_req->result, "uv_fs_open callback");
    
    uv_fs_t* stat_req = malloc(sizeof(uv_fs_t));
    r = uv_fs_stat(open_req->loop, stat_req, open_req->path, NULL);
    CHECK(r, "uv_fs_stat");
    
    fprintf(stdout, "### file size: %ld\n", stat_req->statbuf.st_size);
    
    int nbufs = stat_req->statbuf.st_size / SIZE + 1;
    fprintf(stdout, "### nbuf: %d\n", nbufs);
    
    uv_buf_t* bufs = malloc(sizeof(uv_buf_t)*nbufs);
    int i;
    for(i = 0; i< nbufs-1; i++)
        bufs[i] = uv_buf_init((char*)malloc(stat_req->statbuf.st_size), SIZE);
    bufs[i] = uv_buf_init((char*)malloc(stat_req->statbuf.st_size), stat_req->statbuf.st_size - (nbufs-1)*SIZE);
    for(i = 0; i< nbufs; i++)
        fprintf(stdout, "    buf[%d]: %ld\n", i, bufs[i].len);
    
    uv_fs_t *read_req = malloc(sizeof(uv_fs_t));
    context_t* context = open_req->data;
    context->read_req = read_req;
    read_req->data = context;

    r = uv_fs_read(open_req->loop, read_req, open_req->result, bufs, nbufs, 0, read_file);
    CHECK(r, "uv_fs_read");
}

void read_file(uv_fs_t* read_req) {
    if (read_req->result < 0) CHECK((int)read_req->result, "uv_fs_read callback");
    
    //fprintf(stdout, "bufs->len: %ld\n", read_req->bufs[1].len);
    fprintf(stdout, "### bufs->base:\n%s\n### bufs->len: %ld\n", read_req->bufs[11].base, read_req->bufs[11].len);
    
    context_t* context = read_req->data;
    uv_write_t* sendfile_req = malloc(sizeof(uv_write_t));
    
    int i;
    for(i = 0; i< read_req->nbufs-1; i++) {
        r = uv_write(sendfile_req, context->tcp, read_req->bufs, i, NULL);
        CHECK(r, "uv_fs_write");
        i++;
    }
    
    free(read_req->bufs->base);
    
    uv_fs_t* close_req = malloc(sizeof(uv_fs_t));
    close_req->data = context;

    r = uv_fs_close(read_req->loop, close_req, context->open_req->result, close_file);
    CHECK(r, "uv_fs_close");
}

void close_file(uv_fs_t* close_req) {
    if (close_req->result < 0) CHECK((int)close_req->result, "uv_fs_close callback");

    context_t* context = close_req->data;
    
    uv_fs_req_cleanup(context->open_req);
    uv_fs_req_cleanup(context->read_req);
    uv_fs_req_cleanup(close_req);
    free(context);
}