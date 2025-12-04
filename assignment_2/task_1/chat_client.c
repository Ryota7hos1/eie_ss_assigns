#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "udp.h"

// client code
struct sockaddr_in server_addr;

void* sender_thread(void* arg) {
    int sd = *(int*)arg;
    char buffer[BUFFER_SIZE];
    while (1) {
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer,"\n")] = '\0';
        udp_socket_write(sd, &server_addr, buffer, strlen(buffer));
    }
    return NULL;
}

void* listener_thread(void* arg) {
    int sd = *(int*)arg;
    struct sockaddr_in from_addr;
    char buffer[BUFFER_SIZE];
    while (1) {
        int bytes = udp_socket_read(sd, &from_addr, buffer, BUFFER_SIZE);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            printf("%s\n", buffer);
        }
    }
    return NULL;
}

int main() {
    int sd = udp_socket_open(0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    pthread_t tid_sender, tid_listener;
    pthread_create(&tid_sender, NULL, sender_thread, &sd);
    pthread_create(&tid_listener, NULL, listener_thread, &sd);

    pthread_join(tid_sender, NULL);
    pthread_join(tid_listener, NULL);

    close(sd);
    return 0;
}