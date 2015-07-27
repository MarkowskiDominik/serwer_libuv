/* 
 * File:   main.c
 * Author: dominik
 *
 * Created on 6 lipiec 2015, 11:43
 */

#include <stdlib.h>
#include <stdio.h>
#include <uv.h>
#include <time.h>
#include <string.h>

#define DEFAULT_ADDRESS INADDR_ANY
#define DEFAULT_PORT 1500
#define DEFAULT_BACKLOG 10
#define SIZE 43776
#define LOG_ERROR_FILE "catchup_error.log"
#define LOG_INFO_FILE "catchup_info.log"

#define CHECK(r, msg) if (r) {                                                          \
    FILE *fd;                                                                           \
    fd = fopen(LOG_ERROR_FILE, "a");                                                    \
    if (fd)                                                                             \
        fprintf(fd, "%s: [%s(%d): %s]\n", msg, uv_err_name((r)), r, uv_strerror((r)));  \
    fclose(fd);                                                                         \
    fprintf(stderr, "%s: [%s(%d): %s]\n", msg, uv_err_name((r)), r, uv_strerror((r)));  \
    exit(EXIT_FAILURE);                                                                 \
}

#define LOG_INFO(msg) {                                                                 \
    time_t rawtime;                                                                     \
    time (&rawtime);                                                                    \
    char buffer[20];                                                                    \
    strftime(buffer,20,"%F %T",localtime(&rawtime));                                    \
    FILE *fd;                                                                           \
    fd = fopen(LOG_INFO_FILE, "a");                                                     \
    if (fd)                                                                             \
        fprintf(fd, "%s : %s\n", buffer, msg);                                          \
    fclose(fd);                                                                         \
}

typedef struct context_struct {
    uv_fs_t *open_req;
    uv_fs_t *read_req;
    uv_stream_t* tcp;
} context_t;

void alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf);
void connection(uv_stream_t* server, int status);
void new_thread(void *arg);
void read_file_name(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
void open_file(uv_fs_t*);
void read_file(uv_fs_t*);

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

    uv_loop_t* loop = malloc(sizeof (uv_loop_t));
    r = uv_loop_init(loop);
    CHECK(r, "Loop init");

    uv_tcp_t* server = malloc(sizeof (uv_tcp_t));
    r = uv_tcp_init(loop, server);
    CHECK(r, "TCP init");

    struct sockaddr_in addr;
    uv_ip4_addr(address, port, &addr);
    fprintf(stderr, "settings serwer: %s %d %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), backlog);

    r = uv_tcp_bind(server, (const struct sockaddr*) &addr, 0);
    CHECK(r, "Bind");

    r = uv_listen((uv_stream_t*) server, backlog, connection);
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
    *buf = uv_buf_init((char*) malloc(size), size);
}

void connection(uv_stream_t* server, int status) {
    CHECK(status, "New connection");

    uv_tcp_t* client = malloc(sizeof (uv_tcp_t));
    uv_tcp_init(server->loop, client);

    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_thread_t tid;
        r = uv_thread_create(&tid, new_thread, client);
        CHECK(r, "uv_thread_create\n");
        uv_thread_join(&tid);
    } else {
        uv_close((uv_handle_t*) client, NULL);
    }
}

void new_thread(void* arg) {
    uv_tcp_t* client = (uv_tcp_t*) arg;
    r = uv_read_start((uv_stream_t*) client, alloc_buffer, read_file_name);
    CHECK(r, "uv_read_start\n");
}

void read_file_name(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    /*
    char* info = inet_ntoa(client_sock.sin_addr)  (int)client_sock.sin_port)  buf->base;
    struct sockaddr_in* addr = (struct sockaddr_in*)&client;
    fprintf(stdout, "#### %s %d", inet_ntoa(addr->sin_addr), (int)addr->sin_port);
     */
    LOG_INFO(buf->base);

    if (nread == UV_EOF) {
        uv_close((uv_handle_t*) client, NULL);
    } else if (nread > 0) {
        if (!strcmp(buf->base, "KILL")) uv_stop(client->loop);

        uv_fs_t* open_req = malloc(sizeof (uv_fs_t));
        context_t* context = malloc(sizeof (context_t));
        context->tcp = client;
        context->open_req = open_req;
        open_req->data = context;

        uv_fs_t* access_req = malloc(sizeof (uv_fs_t));
        if (uv_fs_access(client->loop, access_req, buf->base, O_RDONLY, NULL) == 0) {
            r = uv_fs_open(client->loop, open_req, buf->base, O_RDONLY, S_IRUSR, open_file);
            CHECK(r, "uv_fs_open");
        } else {
            uv_write_t write_req;
            uv_buf_t buf = uv_buf_init((char*) malloc(0), 0);
            r = uv_write(&write_req, client, &buf, 1, NULL);
            CHECK(r, "uv_write empty");
            uv_close((uv_handle_t*) client, NULL);
        }
    }
    if (nread == 0) free(buf->base);
}

void open_file(uv_fs_t* open_req) {
    if (open_req->result < 0) CHECK((int) open_req->result, "uv_fs_open callback");

    uv_fs_t* stat_req = malloc(sizeof (uv_fs_t));
    r = uv_fs_stat(open_req->loop, stat_req, open_req->path, NULL);
    CHECK(r, "uv_fs_stat");

    int nbufs;
    if (stat_req->statbuf.st_size % SIZE == 0)
        nbufs = stat_req->statbuf.st_size / SIZE;
    else
        nbufs = stat_req->statbuf.st_size / SIZE + 1;

    uv_buf_t* bufs = malloc(sizeof (uv_buf_t) * nbufs);
    int i;
    for (i = 0; i < nbufs - 1; i++)
        bufs[i] = uv_buf_init((char*) malloc(stat_req->statbuf.st_size), SIZE);
    bufs[i] = uv_buf_init((char*) malloc(stat_req->statbuf.st_size), stat_req->statbuf.st_size % SIZE);

    uv_fs_t *read_req = malloc(sizeof (uv_fs_t));
    context_t* context = open_req->data;
    context->read_req = read_req;
    read_req->data = context;

    uv_buf_t* buf = malloc(sizeof (uv_buf_t));
    if (uv_is_readable((const uv_stream_t*) context->tcp)) {
        r = uv_fs_read(open_req->loop, read_req, open_req->result, bufs, nbufs, 0, read_file);
        CHECK(r, "uv_fs_read");
    }
}

void read_file(uv_fs_t* read_req) {
    if (read_req->result < 0) CHECK((int) read_req->result, "uv_fs_read callback");

    context_t* context = read_req->data;
    uv_write_t write_req;

    r = uv_write(&write_req, context->tcp, read_req->bufs, read_req->nbufs, NULL);
    CHECK(r, "uv_write");

    uv_fs_t close_req;
    close_req.data = context;

    r = uv_fs_close(read_req->loop, &close_req, context->open_req->result, NULL);
    CHECK(r, "uv_fs_close");

    uv_close((uv_handle_t*) context->tcp, NULL);
}