/*
 * This program implements a proxy server that can handle GET requests and headers of all type.
 * It serves clients concurrently uses no control flow, using the serve function.
 *
 * To set up a connection with the client and the server, the wrapper functions open_clientfd and open_listenfd
 * are used
 *
 * The only signal we block in this program is SIGPIPE. This is to prevent unexpected crashes and maintain stability.
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include "http_parser.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1";

typedef struct {
    struct sockaddr_storage addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[MAXLINE];         // Client host
    char port[MAXLINE];         // Client port
} client_info;

void clienterror(int fd, const char *errnum, const char *shortmsg, const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

void *serve(void *vargp) {
    client_info *client = (client_info *) vargp;
    pthread_detach(pthread_self());

    parser_t *parser = parser_new();

    char buf_parser[MAXLINE];

    rio_t client_rio;
    rio_readinitb(&client_rio, client->connfd);
    if (rio_readlineb(&client_rio, buf_parser, MAXLINE) <= 0) {
        return NULL;
    }

    parser_state parse_state = parser_parse_line(parser, buf_parser);

    if (parse_state != REQUEST) {
        parser_free(parser);
        clienterror(client->connfd, "400", "Bad Request", "Proxy received a malformed request");
        return;
    }

    const char *method, *path, *port, *host;
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, HOST, &host);

    if (strncmp(method, "GET", 3)) {
        clienterror(client->connfd, "501", "Not Implemented", "Proxy does not implement this method");
    }

    if (port == NULL) {
        port = "80";
    }

    char request[MAXLINE];
    while (rio_readlineb(&client_rio, buf_parser, MAXLINE) > 2) {
        parse_state = parser_parse_line(parser, buf_parser);
        if (parse_state != HEADER) {
            /* Error with reading request*/
            return NULL;
        }
    }

    snprintf(request, MAXLINE, "GET %s HTTP/1.0\r\n", path);

    header_t *curHeader = parser_retrieve_next_header(parser);
    size_t headers_parsed;
    while (curHeader != NULL) {
        char *header_name = curHeader->name;
        if (strncmp("User-agent", header_name, 10)) {
            char header_line[MAXLINE] = "";
            strncat(header_line, header_name, MAXLINE);
            strncat(header_line, ": ", MAXLINE);
            strncat(header_line, curHeader->value, MAXLINE);
            strncat(header_line, "\r\n", MAXLINE);

            strncat(request, header_line, MAXLINE);
        }
        curHeader = parser_retrieve_next_header(parser);
        headers_parsed++;
    }

    if (headers_parsed < 1) {
        /* Error, not enough headers */
    }

    strncat(request, "User-Agent: ", MAXLINE);
    strncat(request, header_user_agent, MAXLINE);
    strncat(request, "\r\n", MAXLINE);
    strncat(request, "\r\n", MAXLINE);

    int clientfd = open_clientfd(host, port); //Used to communicate with the server
    if (clientfd < 0) {
        clienterror(client->connfd, "503", "Service Unavailable", "Failed to connect to server");
        parser_free(parser);
        return NULL;
    }

    rio_writen(clientfd, request, strlen(request));

    rio_t server_rio;
    rio_readinitb(&server_rio, clientfd);
    char server_response[MAXLINE];
    size_t response_size;
    while ((response_size = rio_readnb(&server_rio, server_response, MAXLINE)) > 0) {
        rio_writen(client->connfd, server_response, response_size);
    }

    close(client->connfd);

    return NULL;
}

int main(int argc, char **argv) {
    /* --- Setting up the Proxy --- */
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    dbg_assert(argc == 2);

    char *listening_port = argv[1];
    int listenfd;
    pthread_t tid;

    listenfd = open_listenfd(listening_port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
    }

    /* --- Handling Requests ---*/
    while (true) {
        client_info *client = Malloc(sizeof(client_info));

        client->addrlen = sizeof(client->addr);

        client->connfd = accept(listenfd, &client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }
        /* Serve an individual client */
        pthread_create(&tid, NULL, serve, client);
    }
}
