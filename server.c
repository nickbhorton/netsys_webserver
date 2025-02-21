#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "debug_macros.h"

#define BACKLOG 128
#define CHUNK_SIZE 16384
static int sfd = -1;
static int cfd = -1;

#define Fatal(rv, call)                                                                                                \
    {                                                                                                                  \
        rv = call;                                                                                                     \
        if (rv < 0) {                                                                                                  \
            fflush(stdout);                                                                                            \
            fflush(stderr);                                                                                            \
            exit(EXIT_FAILURE);                                                                                        \
        }                                                                                                              \
    }

#define PrintErrno(str)                                                                                                \
    int en = errno;                                                                                                    \
    fprintf(stderr, "%s() %s\n", str, strerror(en));

#define FatalCheckErrno(rv, call, call_str)                                                                            \
    {                                                                                                                  \
        rv = call;                                                                                                     \
        if (rv < 0) {                                                                                                  \
            PrintErrno(call_str);                                                                                      \
            fflush(stdout);                                                                                            \
            fflush(stderr);                                                                                            \
            exit(EXIT_FAILURE);                                                                                        \
        }                                                                                                              \
    }

void useage();
void netprint(const char* buffer, size_t size);

// these are fatal thus void
void parent_setup_signal_handlers();
void child_setup_signal_handlers();

int main(int argc, char** argv)
{
    if (argc != 2) {
        useage();
        return 1;
    }

    parent_setup_signal_handlers();

    Address server_address;
    int rv;

    Fatal(sfd, bind_socket(NULL, argv[1], &server_address));
    FatalCheckErrno(rv, listen(sfd, BACKLOG), "listen");

    Address client_address;
    while (1) {
        cfd = accept(sfd, Address_sockaddr(&client_address), &client_address.addrlen);
        if (cfd < 0) {
            int en = errno;
            NP_DEBUG_ERR("accept() %s\n", strerror(en));
            continue;
        }
        int cpid;
        if (!(cpid = fork())) {
            child_setup_signal_handlers();

            close(sfd); // close listener
            cpid = getpid();

            struct pollfd pfd[1];
            pfd[0].fd = cfd;
            pfd[0].events = POLLIN;

            char recv_buff[WS_BUFFER_SIZE];
            size_t recv_buffer_index = 0;
            memset(recv_buff, 0, WS_BUFFER_SIZE);

            char send_buff[CHUNK_SIZE];

            for (size_t r = 0; r < 200; r++) {
                int num_events = poll(pfd, 1, WS_CHILD_TIMEOUT);
                if (num_events == 0) {
                    goto clean_exit;
                }
                if (num_events > 0 && pfd[0].revents & POLLIN) {
                    int recv_count = recv(cfd, recv_buff + recv_buffer_index, WS_BUFFER_SIZE - recv_buffer_index, 0);
                    if (recv_count < 0) {
                        int en = errno;
                        NP_DEBUG_ERR("recv() %s\n", strerror(en));
                        goto clean_exit;
                    } else if (recv_count == 0) {
                        // client has closed the connection
                        goto clean_exit;
                    } else if (http_nlen(recv_buff, WS_BUFFER_SIZE) == WS_BUFFER_SIZE) {
                        // need more data from recv
                        recv_buffer_index += recv_count;
                        continue;
                    }

                    HttpRequest request = HttpRequest_create(recv_buff);
                    HttpResponse response = HttpResponse_create(&request);

                    // send header
                    size_t sent_bytes = 0;
                    while (sent_bytes < response.header.len) {
                        rv = send(
                            cfd,
                            response.header.data + sent_bytes,
                            response.header.len - sent_bytes,
                            MSG_NOSIGNAL
                        );
                        if (rv < 0) {
                            int en = errno;
                            NP_DEBUG_ERR("send() %s\n", strerror(en));
                            goto clean_exit;
                        }
                        sent_bytes += rv;
                    }

                    String_free(&response.header);

                    // send the file in chunks
                    if (response.code == 200 && request.line.method == REQ_METHOD_GET) {
                        FILE* fptr = fopen(request.line.uri, "r");
                        if (fptr == NULL) {
                            // If this is able to fail then stat() does not fail but open does.
                            goto clean_exit;
                        }

                        // for all file chunks
                        for (size_t i = 0; i < (response.finfo.length / CHUNK_SIZE) + 1; i++) {
                            // put bytes into the send buffer
                            int c;
                            size_t j = 0;
                            for (; j < CHUNK_SIZE; j++) {
                                if ((c = fgetc(fptr)) == EOF) {
                                    break;
                                }
                                send_buff[j] = (char)c;
                            }

                            // send the current send buffer to the client
                            sent_bytes = 0;
                            while (sent_bytes < j) {
                                rv = send(cfd, send_buff + sent_bytes, j - sent_bytes, MSG_NOSIGNAL);
                                if (rv < 0) {
                                    int en = errno;
                                    NP_DEBUG_ERR("send() %s\n", strerror(en));
                                    goto clean_exit;
                                }
                                sent_bytes += rv;
                            }
                        }
                        fclose(fptr);
                    }

                    const char* connect_str = "none";
                    if (request.headers.connection == REQ_CONNECTION_KEEP_ALIVE) {
                        connect_str = "keep-alive";
                    } else if (request.headers.connection == REQ_CONNECTION_CLOSE) {
                        connect_str = "close";
                    }
                    NP_DEBUG_MSG(
                        "%i: %s%i%s %-48s Connection: %s\n",
                        cpid,
                        response.code == 200 ? "\e[32m" : "\e[31m",
                        response.code,
                        "\e[0m",
                        request.line.uri,
                        connect_str
                    );

                    if (request.headers.connection != REQ_CONNECTION_KEEP_ALIVE) {
                        goto clean_exit;
                    }

                    // clear recv_buff
                    memset(recv_buff, 0, recv_count);
                    recv_buffer_index = 0;
                }
            }

        clean_exit:
            shutdown(cfd, SHUT_RDWR);
            fflush(stdout);
            fflush(stderr);
            exit(EXIT_SUCCESS);
        }

        // if parent shutdown(clie_fd, SHUT_WR) then childs pipe will break.
        // so child is responsable for shutting down the socket
        close(cfd);
        cfd = -1;
    }
}

// This handler is called when the parent process recieves the SIGCHLD signal
// (which indicates that the child has exited). When a child exits its resources
// need to be cleaned up (this is what the waitpid call will do). If this is not
// done, when the parent exits the child will become a 'zombie'. Meaning it is
// 'dead' (exited) but its resources have not been cleaned up. The dead child is
// reparented to init process.
void sigchld_handler(int signal)
{
    // this handler can screw up errno
    int en = errno;

    pid_t pid = 0;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        NP_DEBUG_MSG("\e[31mSIGCHLD\e[0m pid %i, status %i\n", pid, status);
    }
    // reset errno
    errno = en;
}

void parent_sigint_handler(int signal)
{
    NP_DEBUG_MSG("parent %i SIGINT handler\n", getpid());

    int child_pid = 0;
    int status = 0;
    while (true) {
        child_pid = waitpid(-1, &status, 0);
        if (child_pid == -1 && errno == ECHILD) {
            NP_DEBUG_MSG("parent %i children have all been reaped parent can exit\n", getpid());
            break;
        } else if (child_pid == -1) {
            int en = errno;
            NP_DEBUG_MSG("wait() %s\n", strerror(en));
        } else {
            NP_DEBUG_MSG("\e[31m%i\e[0m reaped child, status %i\n", child_pid, status);
        }
        fflush(stdout);
    }

    int rv = shutdown(sfd, 2);
    if (rv < 0) {
        int en = errno;
        NP_DEBUG_ERR("shutdown() %s\n", strerror(en));
    }
    fflush(stdout);
    fflush(stderr);
    exit(0);
}

void child_sigint_handler(int signal)
{
    int rv = shutdown(cfd, SHUT_RDWR);
    if (rv < 0) {
        int en = errno;
        NP_DEBUG_ERR("shutdown() %s\n", strerror(en));
    }
    NP_DEBUG_MSG("child %i SIGINT recieved, exited cleanly\n", getpid());
    fflush(stdout);
    fflush(stderr);
    exit(0);
}

void parent_setup_signal_handlers()
{
    int rv;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);

    // This flag indicates that if a handler is called in the middle of a
    // systemcall then after the handler is done the systemcall is restarted
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sigchld_handler;
    FatalCheckErrno(rv, sigaction(SIGCHLD, &sa, NULL), "SIGCHLD sigaction()");

    sa.sa_flags = 0;
    sa.sa_handler = parent_sigint_handler;
    FatalCheckErrno(rv, sigaction(SIGINT, &sa, NULL), "parent SIGINT sigaction()");
}

void child_setup_signal_handlers()
{
    int rv;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = SIG_DFL;
    FatalCheckErrno(rv, sigaction(SIGINT, &sa, NULL), "reset child SIGINT sigaction()");
    FatalCheckErrno(rv, sigaction(SIGCHLD, &sa, NULL), "reset child SIGCHILD sigaction()");

    sa.sa_handler = child_sigint_handler;
    FatalCheckErrno(rv, sigaction(SIGINT, &sa, NULL), "child SIGINT sigaction()");
}

void netprint(const char* buffer, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == '\r') {
            printf("\033[41mCR\033[0m");
        } else if (buffer[i] == '\n') {
            printf("\033[41mLF\033[0m\n");
        } else if (buffer[i] == '\0') {
            break;
        } else {
            printf("%c", buffer[i]);
        }
    }
}

void useage() { printf("./server <port number>\n"); }
