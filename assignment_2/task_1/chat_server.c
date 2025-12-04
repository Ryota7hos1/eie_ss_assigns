
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "udp.h"

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr;
    int sd = udp_socket_open(SERVER_PORT);
    getsockname(sd, (struct sockaddr *)&server_addr, BUFFER_SIZE);
    Node* server = create_node("Server", server_addr);
    assert(sd > -1);

    printf("Server is listening on port %d\n", SERVER_PORT);

    // Server main loop
    while (1) 
    {
        char client_request[BUFFER_SIZE], server_msg1[BUFFER_SIZE], server_msg2[BUFFER_SIZE];
        struct sockaddr_in client_address;
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE);
        if (rc > 0)
        {
            char instruction[BUFFER_SIZE];
            char message[BUFFER_SIZE];
            sscanf(client_request, "%[^$]$ %[^\n]", instruction, message);
            if (instruction == "conn") {
                push_back(&server, message, client_address);
                sprintf(server_msg2, "Hi %s, you have succesfully connected to the chat\n", message); 
                ///send a msg to client_address only
                rc = udp_socket_write(sd, &client_address, server_msg2, BUFFER_SIZE);
            }
            else if (instruction == "say") {
                ///go through the list
                ///do a for loop for the muted from list 
                ///if it is found in there don't send a msg to them
                Node* sender = find_node_addr(server, client_address);
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
                    if (!blocked) {
                        snprintf(server_msg2, BUFFER_SIZE, "%s: %s\n", sender->name, message);
                        rc = udp_socket_write(sd, &cur->client_ad, server_msg2, BUFFER_SIZE);
                    }
                    cur = cur->next;
                }
            }
            else if (instruction == "sayto") {
                ///find the sender in the linked list
                ///see if the receiver is in the muted from list
                ///if not, send the msg
                ///split message into 2
                char name1[BUFFER_SIZE];
                char message1[BUFFER_SIZE];
                sscanf(message, "%[^ ] %[^\n]", name1, message1);
                bool blocked = false;
                Node* sender = find_node_addr(server, client_address);
                Node* receiver = find_node_by_name(server, name1);
                BlockNode* cur = sender->blocked_by;
                if (sender != NULL && receiver != NULL) {
                    while (cur != NULL) {
                        if (cur->client == receiver) {
                            blocked = true;
                            break;
                        }
                        cur = cur->next;
                    }
                    if (!blocked) {
                        snprintf(server_msg2, BUFFER_SIZE, "%s: %s\n", sender->name, message1);
                        rc = udp_socket_write(sd, &receiver->client_ad, server_msg2, BUFFER_SIZE);
                    }
                }
            }
            else if (instruction == "disconn") {
                ///take the node out of the linked list and connect the before and next nodes
                ///dereference the node along with the blockNode
                Node* sender = find_node_addr(server, client_address);
                strcpy(server_msg2, "Disconnected. Bye!");
                if (sender != NULL) {
                    rc = udp_socket_write(sd, &client_address, server_msg2, BUFFER_SIZE);
                    disconnect_node(&server, sender);
                }
            }
            else if (instruction == "mute") {//good
                ///each node has a mute_list that stores which nodes it is muted from
                ///this way is better because if we make mute_list store which nodes it is muting,
                /// we will be doing a loop for every possible element in the linked list and it scales horribly (?)
                Node* target = find_node(server, message);
                if (target != NULL) {
                    push_back_blocknode(find_node_addr(server, client_address), target);
                }
            }
            else if (instruction == "unmute") {//good
                ///go to the receiver in the linked list
                ///remove sender from the receiver's muted from list
                ///connect before and next nodes
                Node* blocked = find_node(server, message);
                remove_blocknode(find_node_addr(server, client_address), blocked);
            }
            else if (instruction == "rename") { //good
                ///find sender in linked list
                ///change name to the new name
                Node* name_change = find_node_addr(server, client_address);
                strncpy(name_change->name, message, BUFFER_SIZE);
                name_change->name[BUFFER_SIZE - 1] = '\0';
            }
            else if (instruction == "kick") {
                ///check some kind of authority
                ///disconnect a node
                Node* sender = find_node_addr(server, client_address);
                ///some kind of condition
                Node* target = find_node(server, message);
                if (target != NULL) {
                    strcpy(server_msg2, "You have been removed from the chat");
                    snprintf(server_msg1, BUFFER_SIZE, "%s has been removed from the chat\n", message);
                    rc = udp_socket_write(sd, &target->client_ad, server_msg2, BUFFER_SIZE);
                    disconnect_node(&server, target);
                    Node* cur = server;
                    while (cur != NULL) {
                        rc = udp_socket_write(sd, &cur->client_ad, server_msg1, BUFFER_SIZE);
                        cur = cur->next;
                    }
                }
            }
            printf("Request served...\n");
        }
    }

    return 0;
}