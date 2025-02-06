#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debug_macros.h"

#define BACKLOG 10

static int sfd = -1;
static int cfd = -1;
static pid_t cpid = -1;

void useage();

int setup_signal_handlers();
void sigchld_handler(int signal);
void sigint_handler(int signal);

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

int main(int argc, char** argv)
{
    if (argc != 2) {
        useage();
        return 1;
    }

    Address server_address;
    sfd = bind_socket(NULL, argv[1], &server_address);
    if (sfd < 0) {
        return 1;
    }
    int rv = listen(sfd, BACKLOG);
    if (rv < 0) {
        int en = errno;
        NP_DEBUG_ERR("listen() %s\n", strerror(en));
    }

    rv = setup_signal_handlers();
    if (rv < 0) {
        return -rv;
    }

    Address client_address;
    while (1) {
        cfd = accept(
            sfd,
            Address_sockaddr(&client_address),
            &client_address.addrlen
        );
        if (cfd < 0) {
            int en = errno;
            NP_DEBUG_ERR("accept() %s\n", strerror(en));
            continue;
        }
        if (!(cpid = fork())) {
            cpid = getpid();
            close(sfd); // close listener
            char buffer[WS_BUFFER_SIZE];
            memset(buffer, 0, WS_BUFFER_SIZE);
            rv = recv(cfd, buffer, WS_BUFFER_SIZE, 0);
            if (rv < 0) {
                int en = errno;
                NP_DEBUG_ERR("recv() %s\n", strerror(en));
                goto clean_exit;
            } else if (rv == 0) {
                NP_DEBUG_ERR("%i: client closed connection\n", cpid);
                goto clean_exit;
            }
            WsRequest req = WsRequest_create(buffer);
            // netprint(buffer, WS_BUFFER_SIZE);

            String response = get_response(&req);
            rv = send(cfd, response.data, response.len, MSG_NOSIGNAL);
            if (response.len > 0) {
                NP_DEBUG_MSG("%i: ", cpid);
                size_t i = 0;
                while (response.data[i] != '\r' && i < response.len) {
                    printf("%c", response.data[i]);
                    i++;
                }
                printf("\n");
            }
            String_free(&response);

            if (rv < 0) {
                int en = errno;
                NP_DEBUG_ERR("send() %s\n", strerror(en));
                goto clean_exit;
            }
        clean_exit:
            shutdown(cfd, SHUT_RDWR);
            exit(0);
        }
        // if parent shutdown(clie_fd, SHUT_WR) then childs pipe will break.
        shutdown(cfd, SHUT_RD);
        NP_DEBUG_MSG("\e[32m%i\e[0m spun child\n", cpid);
    }
}

// This handler is called when the parent process recieves the SIGCHLD signal
// (which indicates that the child has exited). When a child exits its resources
// need to be cleaned up (this is what the waitpid call will do). If this is not
// done, when the parent exits the child will become a 'zombie'. Meaning it is
// 'dead' (exited) but its resources have not been cleaned up. The dead child is
// reparented to init process.
//
// This code comes from beej
void sigchld_handler(int signal)
{
    int en = errno;
    // -1 Indicates wait for any child.
    // WNOHANG means 'return immediately if no child exited' (from
    // linux.die.net)
    pid_t child_term_pid;
    pid_t rv = 0;
    while ((rv = waitpid(-1, NULL, WNOHANG)) > 0) {
        child_term_pid = rv;
    }
    errno = en;
    NP_DEBUG_MSG("\e[31m%i\e[0m reaped child\n", child_term_pid);
}

void sigint_handler(int signal)
{
    int rv = shutdown(sfd, 2);
    if (rv < 0) {
        int en = errno;
        NP_DEBUG_ERR("shutdown() %s\n", strerror(en));
    }
    NP_DEBUG_MSG("SIGINT recieved, exited cleanly\n");
    exit(0);
}

int setup_signal_handlers()
{
    // This code comes from beej
    struct sigaction sa_sigchld;
    sa_sigchld.sa_handler = sigchld_handler;
    sigemptyset(&sa_sigchld.sa_mask);
    // This flag indicates that if a handler is called in the middle of a
    // systemcall then after the handler is done the systemcall is restarted
    sa_sigchld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_sigchld, NULL) == -1) {
        int en = errno;
        NP_DEBUG_ERR("sigaction() %s\n", strerror(en));
        return -1;
    }
    struct sigaction sa_sigint;
    sa_sigint.sa_handler = sigint_handler;
    sigemptyset(&sa_sigint.sa_mask);
    if (sigaction(SIGINT, &sa_sigint, NULL) == -1) {
        int en = errno;
        NP_DEBUG_ERR("sigaction() %s\n", strerror(en));
        return -1;
    }
    return 0;
}

void useage() { printf("./server <port number>\n"); }
