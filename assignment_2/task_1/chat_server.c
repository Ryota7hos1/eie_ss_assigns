    #include <stdio.h>
    #include <stdlib.h>
    #include <ctype.h>
    #include <stdbool.h>
    #include "udp.h"
    #include <pthread.h>    
    #include <string.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <assert.h>
    Node* server = NULL;
    CircularBuffer* cb = NULL;
    pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

    void* worker_thread(void* arg) {
        Packet* pkt = (Packet*)arg;
        char instruction[BUFFER_SIZE], message[BUFFER_SIZE];
        char server_reply[BUFFER_SIZE];
        sscanf(pkt->message, "%1023[^$]$ %1023[^\n]", instruction, message);
        Node* sender = find_node_addr(server, pkt->client_addr);
        if (sender != NULL) {
            pthread_mutex_lock(&list_mutex);
            sender->last_active = time(NULL);
            sender->connected = true;
            pthread_mutex_unlock(&list_mutex);
        }
        if (strcmp(instruction, "conn") == 0) {
            bool existing_user = false;
            bool name_duplicate = false;
            Node* cur = server;
            while (cur != NULL) {
                if (strcmp(cur->name, message) == 0) {
                    name_duplicate = true;
                    break;
                }
                cur = cur->next;
            }
            if (!name_duplicate) {
                pthread_mutex_lock(&list_mutex);
                if (sender == NULL) {
                    push_back(&server, message, pkt->client_addr);
                    sender = find_node_addr(server, pkt->client_addr);
                    sender->last_active = time(NULL);
                    sender->connected = true;
                    strcpy(server_reply, "ok");
                }
                else {
                    sender->connected = true;
                    sender->last_active = time(NULL);
                    existing_user = true;
                }
                pthread_mutex_unlock(&list_mutex);
                if (strcmp(message, "") == 0) {
                    snprintf(server_reply, BUFFER_SIZE, "Hi, you have successfully connected to the chat\n");
                }
                else {
                    udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
                    snprintf(server_reply, BUFFER_SIZE, "Hi %.900s, you have successfully connected to the chat\n", message);
                }
                udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
                udp_socket_write(pkt->sd, &pkt->client_addr, "Global history:\n", BUFFER_SIZE);
                pthread_mutex_lock(&cb->lock);
                for (int i = 0; i < cb->count; i++) {
                    int idx = (cb->head + i) % CB_SIZE;
                    udp_socket_write(pkt->sd, &pkt->client_addr, cb->data[idx], BUFFER_SIZE);
                }
                udp_socket_write(pkt->sd, &pkt->client_addr, "------------------", BUFFER_SIZE);
                pthread_mutex_unlock(&cb->lock);
                if (existing_user) {
                    udp_socket_write(pkt->sd, &pkt->client_addr, "Private history:\n", BUFFER_SIZE);
                    pthread_mutex_lock(&sender->history_lock);
                    for (int i = 0; i < sender->hist_count; i++) {
                        int idx = (sender->hist_head + i) % CB_SIZE;
                        udp_socket_write(pkt->sd, &pkt->client_addr, sender->history[idx], BUFFER_SIZE);
                    }
                    udp_socket_write(pkt->sd, &pkt->client_addr, "------------------", BUFFER_SIZE);
                    pthread_mutex_unlock(&sender->history_lock);
                }
            }
            else {
                udp_socket_write(pkt->sd, &pkt->client_addr, "Duplicate", BUFFER_SIZE);
            }
        }
        else if (strcmp(instruction, "say") == 0) {
            snprintf(server_reply, BUFFER_SIZE, "%.100s: %.900s\n", sender->name, message);
            cb_push(cb, server_reply);
            pthread_mutex_lock(&list_mutex);
            Node* cur = server;
            while (cur != NULL) {
                bool blocked = false;
                BlockNode* block = sender->blocked_by;
                while (block != NULL) {
                    if (block->client == cur) {
                        blocked = true;
                        break;
                    }
                    block = block->next;
                }
                if (!blocked && cur->connected) {
                    udp_socket_write(pkt->sd, &cur->client_ad, server_reply, BUFFER_SIZE);
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&list_mutex);
        }
        else if (strcmp(instruction, "sayto") == 0) {
            char name[BUFFER_SIZE-530], msg[BUFFER_SIZE-512];
            sscanf(message, "%[^ ] %[^\n]", name, msg);
            pthread_mutex_lock(&list_mutex);
            Node* receiver = find_node(server, name);
            BlockNode* cur = sender->blocked_by;
            if (receiver != NULL) {
                bool blocked = false;
                while (cur != NULL) {
                    if (cur->client == receiver) {
                        blocked = true;
                        break;
                    }
                    cur = cur->next;
                }
                if (!blocked && receiver->connected) {
                    snprintf(server_reply, BUFFER_SIZE, "%.100s: %.900s\n", sender->name, msg);
                    pthread_mutex_unlock(&list_mutex);
                    udp_socket_write(pkt->sd, &receiver->client_ad, server_reply, BUFFER_SIZE);
                    node_cb_push(sender, server_reply);///locking is done in the funciton itself
                    node_cb_push(receiver, server_reply);
                }
                else {
                    pthread_mutex_unlock(&list_mutex);
                }
            }
            else {
                pthread_mutex_unlock(&list_mutex);
            }
        }
        else if (strcmp(instruction, "disconn") == 0) {
            snprintf(server_reply, BUFFER_SIZE, "Disconnected. Bye!\n");
            udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
            pthread_mutex_lock(&list_mutex);
            disconnect_node(&server, sender);
            pthread_mutex_unlock(&list_mutex);
        }    
        else if (strcmp(instruction, "mute") == 0) {
            pthread_mutex_lock(&list_mutex);
            Node* target = find_node(server, message);
            bool alr_muted = false;
            if (target != NULL) {
                BlockNode* target_blocknode = target->blocked_by;
                while (target_blocknode != NULL) {
                if (target_blocknode->client == sender) {
                    alr_muted = true;
                    target_blocknode = target_blocknode->next;
                }
            }
            if (!alr_muted)
                push_back_blocknode(sender, target);
            }
            pthread_mutex_unlock(&list_mutex);
        }
        else if (strcmp(instruction, "unmute") == 0) {
            pthread_mutex_lock(&list_mutex);
            Node* target = find_node(server, message);
            if (target != NULL) {
                remove_blocknode(sender, target);
            }
            pthread_mutex_unlock(&list_mutex);
        }
        else if (strcmp(instruction, "rename") == 0) {
            pthread_mutex_lock(&list_mutex);
            bool name_exists = false;
            Node* cur = server;
            while (cur != NULL) {
                if (strcmp((cur->name), message) == 0) {
                    name_exists = true;
                    break;
                }
                cur = cur->next;
            }
            if (!name_exists) {
                strncpy(sender->name, message, BUFFER_SIZE);
                sender->name[BUFFER_SIZE - 1] = '\0';
                snprintf(server_reply, BUFFER_SIZE, "You are now known as %.100s\n", sender->name);
            }
            else {
                snprintf(server_reply, BUFFER_SIZE, "The name is already in use");
            }
            pthread_mutex_unlock(&list_mutex);
            udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
        }
        else if (strcmp(instruction, "kick") == 0) {
            if (ntohs(sender->client_ad.sin_port) == 6666) {
                pthread_mutex_lock(&list_mutex);
                Node* target = find_node(server, message);
                if (target != NULL && target->connected) {
                    snprintf(server_reply, BUFFER_SIZE, "You have been removed from the chat");
                    pthread_mutex_unlock(&list_mutex);
                    udp_socket_write(pkt->sd, &target->client_ad, server_reply, BUFFER_SIZE);
                    snprintf(server_reply, BUFFER_SIZE, "%.100s has been removed from the chat\n", target->name);
                    pthread_mutex_lock(&list_mutex);
                    Node* cur = server;
                    while (cur != NULL) {
                        udp_socket_write(pkt->sd, &cur->client_ad, server_reply, BUFFER_SIZE);
                        cur = cur->next;
                    }
                    disconnect_node(&server, target);
                }
                pthread_mutex_unlock(&list_mutex);
            }
        }
        else if (strcmp(instruction, "ret-ping") == 0) {
            pthread_mutex_lock(&list_mutex);
            sender->last_active = time(NULL);
            pthread_mutex_unlock(&list_mutex);
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
                strncpy(pkt->message, buffer, BUFFER_SIZE);
                pthread_t tid;
                pthread_create(&tid, NULL, worker_thread, pkt);
                pthread_detach(tid);
            }
        }
        return NULL;
    }

    void* cleanup_thread(void* arg) {
        int sd = *(int*)arg;
        while (1) {
            sleep(60);
            pthread_mutex_lock(&list_mutex);
            Node* cur = server;
            time_t now = time(NULL);
            while (cur != NULL) {
                Node* nxt = cur->next;
                if ((strcmp(cur->name, "Server") != 0) && cur->connected) {
                    time_t idle = now - cur->last_active;
                    if (idle > 300 && idle <= 359) { 
                        // Send warning ping
                        udp_socket_write(sd, &cur->client_ad, "ping$ You will be disconnected from the chat due to inactivity", BUFFER_SIZE);
                    } 
                    else if (idle > 359) {
                        // Disconnect idle client
                        udp_socket_write(sd, &cur->client_ad, "You have been disconnected from the chat due to inactivity", BUFFER_SIZE);
                        disconnect_node(&server, cur);
                    }
                }
                cur = nxt;
            }
            pthread_mutex_unlock(&list_mutex);
        }
        return NULL;
    }


    int main(int argc, char *argv[])
    {
        struct sockaddr_in server_addr;
        int sd = udp_socket_open(SERVER_PORT);
        socklen_t addr_len = sizeof(server_addr);
        getsockname(sd, (struct sockaddr *)&server_addr, &addr_len);
        server = create_node("Server", server_addr);
        assert(sd > -1);

        cb = malloc(sizeof(CircularBuffer));
        cb_init(cb);

        printf("Server is listening on port %d\n", SERVER_PORT);

        pthread_t listener_tid;
        pthread_create(&listener_tid, NULL, listener_thread, &sd);
        pthread_t cleanup_tid;
        pthread_create(&cleanup_tid, NULL, cleanup_thread, &sd);
        pthread_detach(cleanup_tid);
        pthread_detach(listener_tid);

        while (1) {
            sleep(1);
        }

        close(sd);
        return 0;
    }