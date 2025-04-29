#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEBUG 1
#define MAXSIZE 512
#define CLIENT_PORT_ID 30020

struct command {
    char arg[255];
    char code[5];
};

int socket_create(int port) {
    int sockfd;
    int yes = 1;
    struct sockaddr_in sock_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        return -1;
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        close(sockfd);
        perror("setsockopt() error");
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *) &sock_addr, sizeof(sock_addr)) < 0) {
        close(sockfd);
        perror("bind() error");
        return -1;
    }

    if (listen(sockfd, 5) < 0) {
        close(sockfd);
        perror("listen() error");
        return -1;
    }
    return sockfd;
}

int socket_accept(int sock_listen) {
    int sockfd;
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    sockfd = accept(sock_listen, (struct sockaddr *) &client_addr, &len);

    if (sockfd < 0) {
        perror("accept() error");
        return -1;
    }
    return sockfd;
}

int socket_connect(int port, char* host) {
    int sockfd;
    struct sockaddr_in dest_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error creating socket");
        return -1;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(host);

    if(connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0 ) {
        perror("error connecting to server");
        return -1;
    }
    return sockfd;
}

int recv_data(int sockfd, char* buf, int bufsize) {
    size_t num_bytes;
    memset(buf, 0, bufsize);
    num_bytes = recv(sockfd, buf, bufsize, 0);
    if (num_bytes < 0) {
        return -1;
    }
    return num_bytes;
}

void trimstr(char *str, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (isspace(str[i])) str[i] = 0;
        if (str[i] == '\n') str[i] = 0;
    }
}

int send_response(int sockfd, int rc) {
    int conv = htonl(rc);
    if (send(sockfd, &conv, sizeof conv, 0) < 0 ) {
        perror("error sending...\n");
        return -1;
    }
    return 0;
}

void read_input(char* buffer, int size) {
    char *nl = NULL;
    memset(buffer, 0, size);

    if (fgets(buffer, size, stdin) != NULL) {
        nl = strchr(buffer, '\n');
        if (nl) *nl = '\0';
    }
}

int sock_control;

int read_reply() {
    int retcode = 0;
    if (recv(sock_control, &retcode, sizeof retcode, 0) < 0) {
        perror("client: error reading message from server\n");
        return -1;
    }
    return ntohl(retcode);
}

void print_reply(int rc) {
    switch (rc) {
        case 220:
            printf("220 Welcome, server ready.\n");
            break;
        case 221:
            printf("221 Goodbye!\n");
            break;
        case 226:
            printf("226 Closing data connection. Requested file action successful.\n");
            break;
        case 550:
            printf("550 Requested action not taken. File unavailable.\n");
            break;
    }
}

int ftclient_read_command(char* buf, int size, struct command *cstruct) {
    memset(cstruct->code, 0, sizeof(cstruct->code));
    memset(cstruct->arg, 0, sizeof(cstruct->arg));

    printf("ftclient> ");
    fflush(stdout);

    read_input(buf, size);

    char *arg = NULL;
    arg = strtok(buf," ");
    arg = strtok(NULL, " ");

    if (arg != NULL) {
        strncpy(cstruct->arg, arg, strlen(arg));
    }

    if (strcmp(buf, "list") == 0) {
        strcpy(cstruct->code, "LIST");
    }
    else if (strcmp(buf, "get") == 0) {
        strcpy(cstruct->code, "RETR");
    }
    else if (strcmp(buf, "quit") == 0) {
        strcpy(cstruct->code, "QUIT");
    }
    else {
        return -1;
    }

    memset(buf, 0, 400);
    strcpy(buf, cstruct->code);

    if (arg != NULL) {
        strcat(buf, " ");
        strncat(buf, cstruct->arg, strlen(cstruct->arg));
    }

    return 0;
}

int ftclient_get(int data_sock, int sock_control, char* arg) {
    char data[MAXSIZE];
    int size;
    FILE* fd = fopen(arg, "w");

    while ((size = recv(data_sock, data, MAXSIZE, 0)) > 0) {
        fwrite(data, 1, size, fd);
    }

    if (size < 0) {
        perror("error\n");
    }

    fclose(fd);
    return 0;
}

int ftclient_open_conn(int sock_con) {
    int sock_listen = socket_create(CLIENT_PORT_ID);

    int ack = 1;
    if ((send(sock_con, (char*) &ack, sizeof(ack), 0)) < 0) {
        printf("client: ack write error :%d\n", errno);
        exit(1);
    }

    int sock_conn = socket_accept(sock_listen);
    close(sock_listen);
    return sock_conn;
}

int ftclient_list(int sock_data, int sock_con) {
    size_t num_recvd;
    char buf[MAXSIZE];
    int tmp = 0;

    if (recv(sock_con, &tmp, sizeof tmp, 0) < 0) {
        perror("client: error reading message from server\n");
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    while ((num_recvd = recv(sock_data, buf, MAXSIZE, 0)) > 0) {
            printf("%s", buf);
        memset(buf, 0, sizeof(buf));
    }

    if (num_recvd < 0) {
            perror("error");
    }

    if (recv(sock_con, &tmp, sizeof tmp, 0) < 0) {
        perror("client: error reading message from server\n");
        return -1;
    }
    return 0;
}

int ftclient_send_cmd(struct command *cmd) {
    char buffer[MAXSIZE];
    int rc;

    sprintf(buffer, "%s %s", cmd->code, cmd->arg);

    rc = send(sock_control, buffer, (int)strlen(buffer), 0);
    if (rc < 0) {
        perror("Error sending command to server");
        return -1;
    }

    return 0;
}

void ftclient_login() {
    struct command cmd;
    char user[256];
    memset(user, 0, 256);

    printf("Name: ");
    fflush(stdout);
    read_input(user, 256);

    strcpy(cmd.code, "USER");
    strcpy(cmd.arg, user);
    ftclient_send_cmd(&cmd);

    int wait;
    recv(sock_control, &wait, sizeof wait, 0);

    fflush(stdout);
    char *pass = getpass("Password: ");

    strcpy(cmd.code, "PASS");
    strcpy(cmd.arg, pass);
    ftclient_send_cmd(&cmd);

    int retcode = read_reply();
    switch (retcode) {
        case 430:
            printf("Invalid username/password.\n");
            exit(0);
        case 230:
            printf("Successful login.\n");
            break;
        default:
            perror("error reading message from server");
            exit(1);
            break;
    }
}

int main(int argc, char* argv[]) {
    int data_sock, retcode, s;
    char buffer[MAXSIZE];
    struct command cmd;
    struct addrinfo hints, *res, *rp;

    if (argc != 3) {
        printf("usage: ./ftclient hostname port\n");
        exit(0);
    }

    char *host = argv[1];
    char *port = argv[2];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    s = getaddrinfo(host, port, &hints, &res);
    if (s != 0) {
        printf("getaddrinfo() error %s", gai_strerror(s));
        exit(1);
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock_control = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sock_control < 0)
            continue;

        if(connect(sock_control, res->ai_addr, res->ai_addrlen)==0) {
            break;
        } else {
            perror("connecting stream socket");
            exit(1);
        }
        close(sock_control);
    }
    freeaddrinfo(rp);

    printf("Connected to %s.\n", host);
    print_reply(read_reply());

    ftclient_login();

    while (1) {
        if (ftclient_read_command(buffer, sizeof buffer, &cmd) < 0) {
            printf("Invalid command\n");
            continue;
        }

        if (send(sock_control, buffer, (int)strlen(buffer), 0) < 0 ) {
            close(sock_control);
            exit(1);
        }

        retcode = read_reply();
        if (retcode == 221) {
            print_reply(221);
            break;
        }

        if (retcode == 502) {
            printf("%d Invalid command.\n", retcode);
        } else {
            if ((data_sock = ftclient_open_conn(sock_control)) < 0) {
                perror("Error opening socket for data connection");
                exit(1);
            }

            if (strcmp(cmd.code, "LIST") == 0) {
                ftclient_list(data_sock, sock_control);
            }
            else if (strcmp(cmd.code, "RETR") == 0) {
                if (read_reply() == 550) {
                    print_reply(550);
                    close(data_sock);
                    continue;
                }
                ftclient_get(data_sock, sock_control, cmd.arg);
                print_reply(read_reply());
            }
            close(data_sock);
        }
    }

    close(sock_control);
    return 0;
}