#include <stdio.h>
#include "udp.h"
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#define CLIENT_PORT 0   // this sets the bind() operation in the udp to find a random available port

typedef struct {
    int sd;       // shared between clients
    struct sockaddr_in server_addr; 
    bool connected;
    pthread_mutex_t *mutex;
} thread_args_t;

void *sender_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE], req_type[BUFFER_SIZE], req_cont[BUFFER_SIZE];
    printf("Initial Connection: ");
    
    while(1){  // Loops until client has intially connected to chat
        printf(">> ");
        fgets(client_request, BUFFER_SIZE, stdin);
        // Remove newline if present because server side uses \0 to declare end of request
        client_request[strcspn(client_request, "\n")] = '\0'; 
        
        // Check for connection
        sscanf(client_request, "%[^$]$ %[^\0]", req_type, req_cont); 
        if (!strcmp(req_type, "conn")){
            pthread_mutex_lock(args->mutex);
            args->connected = true;
            pthread_mutex_unlock(args->mutex);
            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
            break;
        }
        printf("Invalid connection command. Please use the format: conn$ YourName\n");
    }

    while (1) {
        printf(">> ");
        fgets(client_request, BUFFER_SIZE, stdin);
        client_request[strcspn(client_request, "\n")] = '\0';  

        // This function writes to the server (sends request)
        // through the socket at sd.
        // (See details of the function in udp.h)
        udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);

        sscanf(client_request, "%[^$]$ %[^\0]", req_type, req_cont);

        // Check for disconnection
        if (strcmp(req_type, "disconn") == 0) { 
            pthread_mutex_lock(args->mutex);
            args->connected = false;
            pthread_mutex_unlock(args->mutex);
            break;
        }
    }
    return NULL;
}

void *listener_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    struct sockaddr_in responder_addr; 
    char server_response[BUFFER_SIZE];

    // Wait for sender thread intial connection
    while(1) {
        // making sure sender side won't write to args->connected while listener is reading
        pthread_mutex_lock(args->mutex);
        bool is_connected = args->connected;
        pthread_mutex_unlock(args->mutex);

        if (is_connected) break;
        
        usleep(10000); // yielding
    }

    while (1) {
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(args->sd, &responder_addr, server_response, BUFFER_SIZE);

        if (rc > 0) {
            printf("server_response: %s", server_response); 
            fflush(stdout); // forces the output stream to immediately write to console and not delay
            
            // Check for disconnection
            pthread_mutex_lock(args->mutex);
            bool is_connected = args->connected;
            pthread_mutex_unlock(args->mutex);

            if (!is_connected) break;

        } else if (rc == -1) {
            // some error happened so exit loop
            pthread_mutex_lock(args->mutex);
            args->connected = false;
            pthread_mutex_unlock(args->mutex);
            break;
        }
    }
    return NULL;
}

// client code
int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number CLIENT_PORT.
    // (See details of the function in udp.h)
    int sd = udp_socket_open(CLIENT_PORT);
    assert(sd > -1);

    // Variable to store the server's IP address and port
    // (i.e. the server we are trying to contact).
    // Generally, it is possible for the responder to be
    // different from the server requested.
    // Although, in our case the responder will
    // always be the same as the server.
    struct sockaddr_in server_addr;

    // Initializing the server's address.
    // We are currently running the server on localhost (127.0.0.1).
    // You can change this to a different IP address
    // when running the server on a different machine.
    // (See details of the function in udp.h)
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    pthread_mutex_t access_mutex = PTHREAD_MUTEX_INITIALIZER;

    thread_args_t args = {
        .sd = sd,
        .server_addr = server_addr,
        .connected = false,
        .mutex = &access_mutex
    };

    pthread_t sender_tid, listener_tid;

    // Create Threads
    pthread_create(&sender_tid, NULL, sender_thread, &args);
    pthread_create(&listener_tid, NULL, listener_thread, &args);

    // Wait for Threads to end
    pthread_join(sender_tid, NULL);
    pthread_join(listener_tid, NULL);

    close(sd); 
    return 0;
}