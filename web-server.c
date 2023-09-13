#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PORT    8000
#define MAX_EVENTS    15000

//const char request[] =
//        "GET /about.html HTTP/1.1"
//        "Host: www.objc.io"
//        "Accept-Encoding: gzip, deflate"
//        "Accept: text/html, application/xtml+xml"
//
// GET /hello.htm HTTP/1.1
// User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)
// Host: www.tutorialspoint.com
// Accept-Language: en-us
// Accept-Encoding: gzip, deflate
// Connection: Keep-Alive

//const char reply[] =
//        "HTTP/1.0 200 OK\r\n"
//        "Content-type: text/html\r\n"
//        "Connection: close\r\n"
//        "Content-Length: 82\r\n"
//        "\r\n"
//        "<html>\n"
//        "<head>\n"
//        "<title>performance test</title>\n"
//        "</head>\n"
//        "<body>\n"
//        "test\n"
//        "</body>\n"
//        "</html>"
//;

//void parse_body(const char *request) {
////    parsed = request.split(' ');
////    method = parsed[0];
////    url = parsed[1];
////    return (method, url);
//}
//
//void generate_headers(const char *method, const char *url) {
////    if method != GET:
////        return ('HTTP/1.1 405 Method not allowed\n\n', 405);
////    if not url in URLS:
////        return ('HTTP/1.1 404 Not Found\n\n', 404);
////    return ('HTTP/1.1 200 OK\n\n', 200);
//}
//
//void generate_body(const char *code, const char *url) {
////    if code == 404:
////        return страница с 404;
////    if code == 405:
////        return страница с кодом 405;
////    return URLS[url];
////    В Urls будет словарь типа "/about": открыть файл достать about
//}
//
//void generate_response(const char *request) {
////    method, url = parse_request(request);
////    headers, code = generate_headers(method, url);
////    body = generate_content(code, url);
////
////    return (headers + body).encode();
//}

void setnonblocking(int fd) {
    /// Change socket mode from block to non-blocking.
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
        puts("\nFailed to change socket mode...\n");
        exit(EXIT_FAILURE);
    }
}

void create_worker() {
    const char reply[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-type: text/html\r\n"
        "Connection: close\r\n"
        "Content-Length: 82\r\n"
        "\r\n"
        "<html>\n"
        "<head>\n"
        "<title>performance test</title>\n"
        "</head>\n"
        "<body>\n"
        "test\n"
        "</body>\n"
        "</html>";

    int worker_fd = socket(AF_INET, SOCK_STREAM, 0);

    const int enable = 1;
    if (setsockopt(worker_fd, SOL_SOCKET, SO_REUSEPORT,
                   &enable, sizeof(enable)) < 0) {
        puts("\nFailed to set server socket option SO_REUSEADDR | SO_REUSEPORT...\n");
        close(worker_fd);
        exit(EXIT_FAILURE);
    }
    setnonblocking(worker_fd);

    struct addrinfo hints;
    struct sockaddr_in worker_addr;
    memset (&hints, 0, sizeof (struct addrinfo));
    worker_addr.sin_family = AF_INET;
    worker_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    worker_addr.sin_port = htons(8000);

    if (bind(worker_fd, (struct sockaddr*)&worker_addr, sizeof(worker_addr)) < 0) { printf ("Error in bind\n"); return; }
    if (listen (worker_fd, 100) < 0) { printf ("Error in listen\n"); return; }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        puts("Failed to create epoll_fd...\n");
        exit(0);
    }

    struct epoll_event event;
    event.data.fd = worker_fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker_fd, &event);

    struct epoll_event *event_list;
    event_list = calloc(MAX_EVENTS, sizeof(event));

    for (;;) {
        int events = epoll_wait(epoll_fd, event_list, MAX_EVENTS, -1);
        for (int i = 0; i < events; ++i) {
            // accept_connection
            if (worker_fd == event_list[i].data.fd) {
                for (;;) {
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    struct sockaddr client_addr;
                    socklen_t client_addr_len;

                    int client_fd = accept(worker_fd, &client_addr, &client_addr_len);
                    if (client_fd == -1) {
                        printf("errno=%d, EAGAIN=%d, EWOULDBLOCK=%d\n", errno, EAGAIN, EWOULDBLOCK);
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            printf("We have processed all incoming connections...\n");
                            break;
                        } else {
                            printf("Accept connection");
                            break;
                        }
                    }

                    if (getnameinfo(&client_addr, client_addr_len,
                        hbuf, sizeof hbuf, sbuf, sizeof sbuf,
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
                    {
                        printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", client_fd, hbuf, sbuf);
                    }

                    setnonblocking(client_fd);

                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET;
                    printf("Set events %u, client_fd=%d\n", event.events, client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
                }
                continue;

            // read_data
            } else if (worker_fd != event_list[i].data.fd) {
                int flag = 0;
                for (;;) {
                    char buffer[512];
                    ssize_t data = read(event_list[i].data.fd, buffer, sizeof(buffer));
                    if (data == -1) {
                        if (errno != EAGAIN) {
                            printf("We have read all data...");
                            flag = 1;
                        }
                        break;
                    } else if (data == 0) {
                        flag = 1;
                        printf("End of file. Remote has closed the connection...");
                        break;
                    }

                    if (write(event_list[i].data.fd, reply, sizeof(reply)) == -1) {
                        puts("Write error...");
                        exit(0);
                    }
                }
                if (flag) {
                    printf("Closed connection on descriptor: %d\n", event_list[i].data.fd);
                    close(event_list[i].data.fd);
                }
            }
        }
    }
}

int main(int argc, char** argv) {
//    create_worker();

    for (int i = 0; i < 5; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            create_worker();
        }
    }

    while (wait(NULL) > 0) {
        if (errno != ECHILD) {
            puts("Wait error...");
            exit(1);
        }
        exit(0);
    }

    return 0;
}
