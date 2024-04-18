/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
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

int main(int argc, char **argv) {
    dbg_assert(argc == 2);
    char *listening_port = argv[1];
    int listenfd;
    client_info client;

    listenfd = open_listenfd(listening_port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
    }

    client.addrlen = sizeof(struct sockaddr_storage);

    client.connfd = accept(listenfd, &client.addr, &client.addrlen);
    if (client.connfd < 0) {
        perror("accept");
    }

    parser_t *parser = parser_new();

    char buf_parser[MAXLINE];

    rio_t client_rio;
    rio_readinitb(&client_rio, client.connfd);
    if (rio_readlineb(&client_rio, buf_parser, MAXLINE) <= 0) {
        /* Error with reading request */
        return 0;
    }

    parser_state parse_state = parser_parse_line(parser, buf_parser);

    if (parse_state != REQUEST) {
        parser_free(parser);
        /* Respond with malformed request */
        return;
    }

    /* We only care about the METHOD and PATH from the request */
    const char *method, *path, *port, *host;
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, HOST, &host);

    if (strncmp(method, "GET", 3)) {
        //Not a GET request
        if (!strncmp(method, "POST", 3)) {
            /* 501 Not Implemented status code */
        }
    }

    if (port == NULL) {
        port = "80";
    }

    char request[MAXLINE];
    while (rio_readlineb(&client_rio, buf_parser, MAXLINE) <= 0) {
        parse_state = parser_parse_line(parser, buf_parser);
        if (parse_state != HEADER) {
            /* Error with reading request*/
            return 0;
        }
    }

    size_t request_len = snprintf(request, MAXLINE, "GET %s HTTP/1.0\r\n", path);

    header_t *curHeader;
    size_t headers_parsed;
    while ((curHeader = parser_retrieve_next_header(parser)) != NULL) {
        char *header_name = curHeader->name;

        if (!strncmp("User-Agent", header_name, 10)) {
            char *header_line[MAXLINE];
            strncat(header_line, header_name, MAXLINE);
            strncat(header_line, ": ", MAXLINE);
            strncat(header_line, curHeader->value, MAXLINE);
            strncat(header_line, "\r\n", MAXLINE);

            request_len += 4 + strnlen(curHeader->value, MAXLINE) + strnlen(header_name, MAXLINE);
            strncat(request, header_line, MAXLINE);
        }
        headers_parsed++;
    }

    strncat(request, "User-Agent: ", MAXLINE);
    strncat(request, header_user_agent, MAXLINE);
    strncat(request, "\r\n", MAXLINE);
    strncat(request, "\r\n", MAXLINE);
    request_len += 2 + strlen(header_user_agent) + 12 + 2;

    int clientfd = open_clientfd(host, port); //Used to communicate with the server

    rio_writen(clientfd, request, request_len);

    rio_t server_rio;
    rio_readinitb(&server_rio, clientfd);
    char server_response[MAXLINE];
    size_t response_size = rio_readnb(&server_rio, server_response, MAXLINE);

    rio_writen(client.connfd, server_response, response_size);
}
