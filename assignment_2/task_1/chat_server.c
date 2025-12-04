
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
                push_back(server, message, client_address);
                sprintf(server_msg2, "Hi %s, you have succesfully connected to the chat\n", message); 
                ///send a msg to client_address only
                rc = udp_socket_write(sd, &client_address, server_msg2, BUFFER_SIZE);
            }
            else if (instruction == "say") {
                ///go through the list
                ///do a for loop for the muted from list 
                ///if it is foud in there don't send a msg to them
            }
            else if (instruction == "sayto") {
                ///find the sender in the linked list
                ///see if the receiver is in the muted from list
                ///if not, send the msg
            }
            else if (instruction == "disconn") {
                ///take the node out of the linked list and connect the before and next nodes
            }
            else if (instruction == "mute") {
                ///each node has a mute_list that stores which nodes it is muted from
                ///this way is better because if we make mute_list store which nodes it is muting,
                /// we will be doing a loop for every possible element in the linked list and it scales horribly (?)
                Node* target = find_node(server, message);
                if (target != NULL) {
                    push_back_blocknode(find_node_addr(server, client_address), target);
                }
            }
            else if (instruction == "unmute") {
                ///go to the receiver in the linked list
                ///remove sender from the receiver's muted from list
                ///connect before and next nodes
                
            }
            else if (instruction == "rename") {
                ///find sender in linked list
                ///change name to the new name
            }
            else if (instruction == "kick") {
                ///check some kind of authority
                ///disconnect a node
            }

            // This function writes back to the incoming client,
            // whose address is now available in client_address, 
            // through the socket at sd.
            // (See details of the function in udp.h)
            rc = udp_socket_write(sd, &client_address, server_msg1, BUFFER_SIZE);

            // Demo code (remove later)
            printf("Request served...\n");
        }
    }

    return 0;
}