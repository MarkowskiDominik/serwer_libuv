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
#define BUFSIZE 1024


#define CHECK(r, msg) if (r) {                                                          \
    fprintf(stderr, "%s: [%s(%d): %s]\n", msg, uv_err_name((r)), r, uv_strerror((r)));  \
    exit(1);                                                                            \
}

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

typedef struct {
    ssize_t nread;
    ssize_t nwritten;
    ssize_t size;
    char *file_path;
    uv_pipe_t *file_pipe;
    uv_stream_t *client;
} file_client_pipe_t;

void connection(uv_stream_t *server, int status);
void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf);
void read_name(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
void file_stat(uv_fs_t* stat_req);
void init_file_client_pipe(uv_stream_t* tcp, char* file_path, ssize_t size);

uv_loop_t *loop;
char *address;
int port;
int backlog;
int r;

int main(int argc, char * argv[]) {
    if (argv[1] != "0") address = argv[1];
    else address = DEFAULT_ADDRESS;
    if (argv[2] != "0") port = atoi(argv[2]);
    else port = DEFAULT_PORT;
    if (argv[3] != "0") backlog = atoi(argv[3]);
    else backlog = DEFAULT_BACKLOG;

    loop = uv_default_loop();

    uv_tcp_t server;
    r = uv_tcp_init(loop, &server);
    CHECK(r, "TCP init");

    struct sockaddr_in addr;
    uv_ip4_addr(address, port, &addr);
    fprintf(stderr, "settings serwer: %s %d %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), backlog);

    r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
    CHECK(r, "Bind");

    r = uv_listen((uv_stream_t*) & server, backlog, connection);
    CHECK(r, "Listen");

    r = uv_run(loop, UV_RUN_DEFAULT);
    CHECK(r, "Run");

    return EXIT_SUCCESS;
}

void connection(uv_stream_t *server, int status) {
    CHECK(status, "New connection");

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof (uv_tcp_t));
    uv_tcp_init(loop, client);

    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_read_start((uv_stream_t*) client, alloc_buffer, read_name);
    } else {
        uv_close((uv_handle_t*) client, NULL);
    }
}

void alloc_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
    *buf = uv_buf_init((char*) malloc(size), size);
}

void read_name(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread == UV_EOF) {
        uv_close((uv_handle_t*) client, NULL);
    } else if (nread > 0) {
        fprintf(stderr, "read %ld bytes: %s\n", nread, buf->base);

        if (!strcmp(buf->base, "KILL")) uv_stop(loop);

        uv_fs_t *stat_req = (uv_fs_t*) malloc(sizeof (uv_fs_t));
        stat_req->data = (void*) client;
        r = uv_fs_stat(loop, stat_req, buf->base, file_stat);
        CHECK(r, "File stat");
    }
    if (nread == 0) free(buf->base);
}

void file_stat(uv_fs_t* stat_req) {
    CHECK((int) stat_req->result, "Stat file");
    
    uv_stream_t *client = (uv_stream_t*) stat_req->data;
    char *path = (char*) malloc(strlen(stat_req->path)); strcpy(path, stat_req->path);
    ssize_t size = stat_req->statbuf.st_size;
    uv_fs_req_cleanup(stat_req);
    
    init_file_client_pipe(client, path, size);
    //write_(client);
}

void init_file_client_pipe(uv_stream_t* client, char* file_path, ssize_t size) {
    uv_pipe_t *file_pipe = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
    uv_pipe_init(loop, file_pipe, NOIPC);
    uv_fs_t file_open_req;
    uv_pipe_open(file_pipe, uv_fs_open(loop, &file_open_req, file_path, O_RDONLY, 0644, NULL));
    
    file_client_pipe_t *file_cp =  (file_client_pipe_t*) malloc(sizeof(file_client_pipe_t));
    file_cp->nread = 0;
    file_cp->nwritten = 0;
    file_cp->size = size;
    file_cp->client = client;
    file_cp->file_pipe = file_pipe;
    
    // allow us to get back to all info regarding this pipe via tcp->data or file_pipe->data
    client->data = (void*) file_cp;
    file_pipe->data = (void*) file_cp;
}
