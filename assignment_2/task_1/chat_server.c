
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "udp.h"

int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number SERVER_PORT
    // (See details of the function in udp.h)
    int sd = udp_socket_open(SERVER_PORT);
    

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

            }
            else if (instruction == "say") {

            }
            else if (instruction == "sayto") {

            }
            else if (instruction == "disconn") {

            }
            else if (instruction == "mute") {

            }
            else if (instruction == "unmute") {

            }
            else if (instruction == "rename") {

            }
            else if (instruction == "kick") {

            }
            strcpy(server_response, "Hi, the server has received: ");
            char ip[BUFFER_SIZE];
            inet_ntop(AF_INET, &client_address.sin_addr, ip, BUFFER_SIZE);
            strcat(server_response, ip);
            strcat(server_response, "\n");
            strcat(server_response, client_request);
            strcat(server_response, "\n");
            bool low = false;
            bool up = false;
            for (int i = 0; message[i] != '\0'; i++) {
                if (islower(message[i])){
                    low = true;
                }
                else if (isupper(message[i])){
                    up = true;
                }
            }
            if (low && up) {
                strcat(server_response, "the msg was: mixed");
                strcat(server_response, "\n");
            }
            else if (low) {
                strcat(server_response, "the msg was: lower-case");
                strcat(server_response, "\n");
            }
            else if (up) {
                strcat(server_response, "the msg was: upper-case");
                strcat(server_response, "\n");
            }
            else {
                strcat(server_response, "error");
                strcat(server_response, "\n");
            }

            // This function writes back to the incoming client,
            // whose address is now available in client_address, 
            // through the socket at sd.
            // (See details of the function in udp.h)
            rc = udp_socket_write(sd, &client_address, server_response, BUFFER_SIZE);

            // Demo code (remove later)
            printf("Request served...\n");
        }
    }

    return 0;
}