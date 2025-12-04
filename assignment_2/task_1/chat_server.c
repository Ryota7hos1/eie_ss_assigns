
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "udp.h"
#include <pthread.h>

Node* server = NULL;

void* worker_thread(void* arg) {
    Packet* pkt = (Packet*)arg;
    char instruction[BUFFER_SIZE], message[BUFFER_SIZE];
    char server_reply[BUFFER_SIZE];
    sscanf(pkt->message, "%[^$]$ %[^\n]", instruction, message);
    Node* sender = find_node_addr(server, pkt->client_addr);
    if (strcmp(instruction, "conn") == 0) {
        push_back(&server, message, pkt->client_addr);
        snprintf(server_reply, BUFFER_SIZE, "Hi %s, you have successfully connected to the chat\n", message);
        udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
    }
    else if (strcmp(instruction, "say") == 0) {
        Node* cur = server;
        while (cur != NULL) {
            bool blocked = false;
            BlockNode* block = cur->blocked_by;
            while (block != NULL) {
                if (block->client == sender) {
                    blocked = true;
                break;
                }
            block = block->next;
            }
        if (!blocked) {
            snprintf(server_reply, BUFFER_SIZE, "%s: %s\n", sender->name, message);
            udp_socket_write(pkt->sd, &cur->client_ad, server_reply, BUFFER_SIZE);
        }
        cur = cur->next;
        }
    }
    else if (strcmp(instruction, "sayto") == 0) {
        char name[BUFFER_SIZE], msg[BUFFER_SIZE];
        sscanf(message, "%[^ ] %[^\n]", name, msg);
        Node* receiver = find_node_by_name(server, name);
        BlockNode* cur = sender->blocked_by;
        if (sender != NULL && receiver != NULL) {
            bool blocked = false;
            while (cur != NULL) {
                if (cur->client == sender) {
                    blocked = true;
                    break;
                }
                cur = cur->next;
            }
            if (!blocked) {
                snprintf(server_reply, BUFFER_SIZE, "%s: %s\n", sender->name, msg);
                udp_socket_write(pkt->sd, &receiver->client_ad, server_reply, BUFFER_SIZE);
            }
        }
    }
    else if (strcmp(instruction, "disconn") == 0) {
        if (sender != NULL) {
            snprintf(server_reply, BUFFER_SIZE, "Disconnected. Bye!\n");
            udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
            disconnect_node(&server, sender);
        }    
    }
    else if (strcmp(instruction, "mute") == 0) {
        Node* target = find_node(server, message);
        if (target != NULL) {
            push_back_blocknode(sender, target);
        }
    }
    else if (strcmp(instruction, "unmute") == 0) {
        Node* target = find_node(server, message);
        if (target != NULL) {
            remove_blocknode(sender, target);
        }
    }
    else if (strcmp(instruction, "rename") == 0) {
        strncpy(sender->name, message, BUFFER_SIZE);
        sender->name[BUFFER_SIZE - 1] = '\0';
    }
    free(pkt);
    return NULL;
}

void* listener_thread(void* arg) {
    int sd = *(int*)arg;
    while (1) {
        struct sockaddr_in client_addr;
        char buffer[BUFFER_SIZE];
        int rc = udp_socket_read(sd, &client_addr, buffer, BUFFER_SIZE);
        if (rc > 0) {
            buffer[rc] = '\0';
            Packet* pkt = malloc(sizeof(Packet));
            pkt->sd = sd;
            pkt->client_addr = client_addr;
            strncpy(pkt->message, rc, BUFFER_SIZE);
            pthread_t tid;
            pthread_create(&tid, NULL, worker_thread, pkt);
            pthread_detach(tid);
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr;
    int sd = udp_socket_open(SERVER_PORT);
    getsockname(sd, (struct sockaddr *)&server_addr, BUFFER_SIZE);
    Node* server = create_node("Server", server_addr);
    assert(sd > -1);

    printf("Server is listening on port %d\n", SERVER_PORT);

    pthread_t listener_tid;
    pthread_create(&listener_tid, NULL, listener_thread, &sd);
    pthread_detach(listener_tid);

    while (1) {
        sleep(1);
    }

    close(sd);
    return 0;
}