#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

const char socket_path[] = "/tmp/test.sock";

int main(void) {
    struct stat statbuf = {0};
    if (stat(socket_path, &statbuf) == 0) {
        unlink(socket_path);
    } else if (errno != ENOENT) {
        perror("socket stat");
        return 1;
    }

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        perror("server socket");
        return 1;
    }

    struct sockaddr_un server_addr = {0};
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, socket_path);

    if (bind(server, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("server bind");
        return 1;
    }

    if (listen(server, 16) < 0) {
        perror("server listen");
        return 1;
    }

    int pid = fork();
    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            perror("client socket");
            return 1;
        }

        struct sockaddr_un client_addr = {0};
        client_addr.sun_family = AF_UNIX;
        strcpy(client_addr.sun_path, socket_path);

        if (connect(client, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            perror("client connect");
            return 1;
        }

        char buffer[32];
        if (read(client, buffer, 32) < 0) {
            perror("client read");
            return 1;
        }

        printf("Received from server: '%s'\n", buffer);

        if (write(client, "Hello server!", 13) < 0) {
            perror("client write");
            return 1;
        }

        return 0;
    } else {
        struct sockaddr_un client_addr = {0};
        socklen_t client_addr_len = 0;
        int client = accept(server, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client < 0) {
            perror("server accept");
            return 1;
        }

        if (write(client, "Hello world!", 12) < 0) {
            perror("server write");
            return 1;
        }

        char buffer[32];
        if (read(client, buffer, 32) < 0) {
            perror("server read");
            return 1;
        }

        printf("Received from client: '%s'\n", buffer);
        return 0;
    }
}
