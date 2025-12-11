// Multithreaded UDP chat server with:
//   Listener thread - spawns worker threads for each packet
//   Worker threads - handle commands (conn, say, sayto, mute, kick…)
//   Cleanup thread - removes inactive clients
//   Linked-list user registry with per-user circular buffers
//   Mute/unmute via per-user BlockNode lists
    #define _XOPEN_SOURCE 700    
    #include <stdio.h>
    #include <stdlib.h>
    #include <ctype.h>
    #include <stdbool.h>
    #include "udp.h"   
    #include <string.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <assert.h>
    #include <pthread.h> 

    // Global pointer to the head of the linked list of clients.
    // The server itself is stored as Node index 0.
    Node* server = NULL;
    Node* dis_node = NULL;
    // Mutex protecting the linked list and all Node metadata.
    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

    // Worker Thread
    // Each packet received by the listener spawns a worker thread.
    // This isolates expensive operations and prevents blocking the listener.
    void* worker_thread(void* arg) {
        Packet* pkt = (Packet*)arg; // Data passed from the listener thread

        // buffers to store parsed data
        char instruction[BUFFER_SIZE];
        char message[BUFFER_SIZE];
        char server_reply[BUFFER_SIZE];

        // Parse client packet as "instruction$ message"
        sscanf(pkt->message, "%1023[^$]$ %1023[^\n]", instruction, message);
        // Identify the sender in the linked list based on sockaddr_in
        pthread_rwlock_rdlock(&rwlock);
        Node* sender = find_node_addr(server, pkt->client_addr);
        pthread_rwlock_unlock(&rwlock);
        // Identify the sender in the linked list based on sockaddr_in
        if (sender != NULL) {
            pthread_rwlock_wrlock(&rwlock);
            sender->last_active = time(NULL);
            pthread_rwlock_unlock(&rwlock);
        }

        // Connection Handler: conn$ name
        // Handles:
        //   • new user connecting
        //   • returning user reconnecting
        //   • duplicate/invalid name rejection
        if (strcmp(instruction, "conn") == 0) {
            bool existing_user = false;
            bool name_duplicate = false;
            // Check if name already exists
            Node* cur = server;
            pthread_rwlock_rdlock(&rwlock);
            while (cur != NULL) {
                if (strcmp(cur->name, message) == 0) {
                    name_duplicate = true;
                    break;
                }
                cur = cur->next;
            }
            Node* dis_list_node = find_node_addr(dis_node, pkt->client_addr);
            pthread_rwlock_unlock(&rwlock);
            if (dis_list_node != NULL) {
                sender = dis_list_node;
                pthread_rwlock_wrlock(&rwlock);
                change_node_conn(&dis_node, &server, dis_list_node, true);
                pthread_rwlock_unlock(&rwlock);
                existing_user = true;
            }
            if (sender!= NULL) {
                existing_user = true;
            }
            // If name is unique OR sender already known (reconnect)
            if (!name_duplicate || existing_user) {
                pthread_rwlock_wrlock(&rwlock);
                // New client, not previously connected
                if (sender == NULL) {
                    push_back(&server, message, pkt->client_addr);
                    sender = find_node_addr(server, pkt->client_addr);
                    sender->last_active = time(NULL);
                    sender->connected = true;
                    strcpy(server_reply, "ok");  // allow client to exit initial thread
                }
                else {
                    // Returning user reconnecting
                    sender->connected = true;
                    sender->last_active = time(NULL);
                    existing_user = true;
                }
                pthread_rwlock_unlock(&rwlock);
                if (strcmp(message, "") == 0) {
                    snprintf(server_reply, BUFFER_SIZE, "Welcome back, you have successfully connected to the chat\n");
                }
                else {
                    udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
                    snprintf(server_reply, BUFFER_SIZE, "Hi %.900s, you have successfully connected to the chat\n", message);
                }
                // Send the greeting
                udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
                // Global History
                udp_socket_write(pkt->sd, &pkt->client_addr, "Global history:\n", BUFFER_SIZE);
                pthread_mutex_lock(&server->history_lock);
                for (int i = 0; i < server->hist_count; i++) {
                    int idx = (server->hist_head + i) % CB_SIZE;
                    udp_socket_write(pkt->sd, &pkt->client_addr, server->history[idx], BUFFER_SIZE);
                }
                udp_socket_write(pkt->sd, &pkt->client_addr, "------------------", BUFFER_SIZE);
                pthread_mutex_unlock(&server->history_lock);
                // Private History
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
            else { // Duplicate name
                udp_socket_write(pkt->sd, &pkt->client_addr, "Duplicate", BUFFER_SIZE);
            }
        }
        // Global Message: say$ message
        // Broadcast to all clients except those who have muted the sender.
        else if (strcmp(instruction, "say") == 0) {
            snprintf(server_reply, BUFFER_SIZE, "%.100s: %.900s\n", sender->name, message);
            // Push into global history (stored in server node)
            node_cb_push(server, server_reply); // mutex inside function
            
            pthread_rwlock_rdlock(&rwlock);
            Node* cur = server;
            while (cur != NULL) {
                bool blocked = false;
                // Check if receiver has muted the sender
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
            pthread_rwlock_unlock(&rwlock);
        }
        // Private Message: sayto$ target message
        // Delivered only to a specific user.
        else if (strcmp(instruction, "sayto") == 0) {
            char name[BUFFER_SIZE-530], msg[BUFFER_SIZE-512];
            sscanf(message, "%[^ ] %[^\n]", name, msg);
            
            pthread_rwlock_rdlock(&rwlock);
            Node* receiver = find_node(server, name);
            if (strcmp(receiver->name, "Server") == 0) {
                snprintf(server_reply, BUFFER_SIZE, "Can't send the server a private message\n");
                pthread_rwlock_unlock(&rwlock);
                udp_socket_write(pkt->sd, &sender->client_ad, server_reply, BUFFER_SIZE);
                return NULL;
            }
            
            // Check if sender is muted by receiver
            BlockNode* cur = sender->blocked_by;
            if ((receiver != NULL)) {
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
                    pthread_rwlock_unlock(&rwlock);
                    udp_socket_write(pkt->sd, &receiver->client_ad, server_reply, BUFFER_SIZE);
                    node_cb_push(sender, server_reply);///locking is done in the funciton itself
                    node_cb_push(receiver, server_reply);
                }
                else {
                    pthread_rwlock_unlock(&rwlock);
                }
            }
            else {
                pthread_rwlock_unlock(&rwlock);
            }
        }
        // Disconnect: disconn$
        else if (strcmp(instruction, "disconn") == 0) {
            snprintf(server_reply, BUFFER_SIZE, "Disconnected. Bye!\n");
            udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
            pthread_rwlock_wrlock(&rwlock);
            change_node_conn(&server, &dis_node, sender, false);
            pthread_rwlock_unlock(&rwlock);
        }    
        // Mute / Unmute
        else if (strcmp(instruction, "mute") == 0) {
            bool alr_muted = false;
            pthread_rwlock_wrlock(&rwlock);
            Node* target = find_node(server, message);
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
            pthread_rwlock_unlock(&rwlock);
        }
        else if (strcmp(instruction, "unmute") == 0) {
            pthread_rwlock_wrlock(&rwlock);
            Node* target = find_node(server, message);
            if (target != NULL) {
                remove_blocknode(sender, target);
            }
            pthread_rwlock_unlock(&rwlock);
        }
        // Rename User: rename$ newname
        else if (strcmp(instruction, "rename") == 0) {
            bool name_exists = false;
            pthread_rwlock_wrlock(&rwlock);
            Node* cur = server;
            // Check if name is already in use
            while (cur != NULL) {
                if (strcmp((cur->name), message) == 0) {
                    name_exists = true;
                    break;
                }
                cur = cur->next;
            }
            cur = dis_node;
            char name[BUFFER_SIZE];
            char extra[BUFFER_SIZE];
            int n = sscanf(message, "%s %s", name, extra);
            if (!name_exists) {
                while (cur != NULL) {
                    if (strcmp((cur->name), message) == 0) {
                        name_exists = true;
                        break;
                    }
                    cur = cur->next;
                }
            }
            if (!name_exists) {
                strncpy(sender->name, message, BUFFER_SIZE);
                sender->name[BUFFER_SIZE - 1] = '\0';
                snprintf(server_reply, BUFFER_SIZE, "You are now known as %.100s\n", sender->name);
            }
            else {
                snprintf(server_reply, BUFFER_SIZE, "The name is already in use");
            }
            if (n == 2) {
                snprintf(server_reply, BUFFER_SIZE, "Please enter a valid name");
            }
            pthread_rwlock_unlock(&rwlock);
            udp_socket_write(pkt->sd, &pkt->client_addr, server_reply, BUFFER_SIZE);
        }
        // Admin Kick: kick$ username
        // Only allowed if sender port == 6666
        else if (strcmp(instruction, "kick") == 0) {
            if (ntohs(sender->client_ad.sin_port) == 6666) {
                pthread_rwlock_rdlock(&rwlock);
                Node* target = find_node(server, message);
                if (target != NULL && target->connected) {
                    // Notify the target
                    snprintf(server_reply, BUFFER_SIZE, "You have been removed from the chat");
                    pthread_rwlock_unlock(&rwlock);
                    udp_socket_write(pkt->sd, &target->client_ad, server_reply, BUFFER_SIZE);
                    // Notify everyone
                    snprintf(server_reply, BUFFER_SIZE, "%.100s has been removed from the chat\n", target->name);
                    pthread_rwlock_wrlock(&rwlock);
                    Node* cur = server;
                    while (cur != NULL) {
                        udp_socket_write(pkt->sd, &cur->client_ad, server_reply, BUFFER_SIZE);
                        cur = cur->next;
                    }
                    change_node_conn(&server, &dis_node, target, false);
                }
                pthread_rwlock_unlock(&rwlock);
            }
        }
        // ret-ping — client returns a heartbeat to stay connected
        else if (strcmp(instruction, "ret-ping") == 0) {
            pthread_rwlock_wrlock(&rwlock);
            sender->last_active = time(NULL);
            pthread_rwlock_unlock(&rwlock);
        }
        free(pkt);
        return NULL;
    }

    // Listener Thread
    // Reads packets using udp_socket_read() (blocking) and spawns workers.
    void* listener_thread(void* arg) {
        int sd = *(int*)arg;
        while (1) {
            struct sockaddr_in client_addr;
            char buffer[BUFFER_SIZE];
            int rc = udp_socket_read(sd, &client_addr, buffer, BUFFER_SIZE);
            if (rc > 0) {
                buffer[rc] = '\0'; // null-terminate
                // Allocate packet object for worker thread
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

    // Cleanup Thread
    // Runs every 60 seconds and:
    //   • Warns clients inactive > 5 minutes (sends ping request)
    //   • Disconnects clients inactive > 6 minutes
    void* cleanup_thread(void* arg) {
        int sd = *(int*)arg;
        while (1) {
            sleep(60);
            pthread_rwlock_wrlock(&rwlock);
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
                        change_node_conn(&server, &dis_node, cur, false);
                    }
                }
                cur = nxt;
            }
            pthread_rwlock_unlock(&rwlock);
        }
        return NULL;
    }

    // Main — Initializes server, creates listener + cleanup threads
    int main(int argc, char *argv[])
    {
        struct sockaddr_in server_addr;
        int sd = udp_socket_open(SERVER_PORT);
        
        socklen_t addr_len = sizeof(server_addr);
        getsockname(sd, (struct sockaddr *)&server_addr, &addr_len);
        // Create server node at head of linked list
        server = create_node("Server", server_addr);
        dis_node = create_node("Disconnect Node Head", server_addr);
        assert(sd > -1);

        printf("Server is listening on port %d\n", SERVER_PORT);

        pthread_t listener_tid;
        pthread_create(&listener_tid, NULL, listener_thread, &sd);

        pthread_t cleanup_tid;
        pthread_create(&cleanup_tid, NULL, cleanup_thread, &sd);

        pthread_detach(cleanup_tid);
        pthread_detach(listener_tid);

        // Keep main alive
        while (1) {
            sleep(1);
        }

        close(sd);
        return 0;
    }