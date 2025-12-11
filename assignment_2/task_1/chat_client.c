// Multithreaded UDP chat client with:
//   ncurses-based UI with separate input/output windows
//   Three-thread architecture:
//      1. initial_thread  – handles initial "conn$NAME" until accepted
//      2. sender_thread   – continuously reads user input and sends commands
//      3. listener_thread – continuously receives packets from server

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <ncurses.h>
#include "udp.h"   //UDP helper functions and structures + originals

// Thread argument structure
//   sd           - socket descriptor
//   server_addr  - address of the server
//   connected    - whether this client is currently connected
//   mutex        - protects modifications to 'connected'
typedef struct {
    int sd;
    struct sockaddr_in server_addr;
    bool connected;
    pthread_mutex_t mutex;
} thread_args_t;

// Global ncurses UI windows & mutex for safe concurrent updates
WINDOW *win_input, *win_output;
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

// initial_thread
// This thread runs only once
// It keeps prompting the user until they successfully connect using: conn$ NAME
// The server must reply with "ok" for the client to proceed.
// Upon success, it sets args->connected = true.
void *initial_thread(void *arg) { //initial loop
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE];
    char req_type[BUFFER_SIZE];
    char req_cont[BUFFER_SIZE];
    char name_check[BUFFER_SIZE];
    while (1) {
        pthread_mutex_lock(&ncurses_mutex);
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 1, ">> ");
        wmove(win_input, 1, 4);
        wrefresh(win_input);
        echo(); // Allow user to see typed characters
        pthread_mutex_unlock(&ncurses_mutex);
        // Read input
        wgetnstr(win_input, client_request, BUFFER_SIZE - 1);

        pthread_mutex_lock(&ncurses_mutex);
        noecho(); // Stop echoing input
        pthread_mutex_unlock(&ncurses_mutex);

        // Remove trailing newline
        client_request[strcspn(client_request, "\n")] = '\0';
        // Extract type and content (e.g., conn$ name)
        int n = sscanf(client_request, "%[^$]$ %s %s", req_type, req_cont, name_check);
        // --- Validate connection command ---
        if (strcmp(req_type, "conn") == 0) {
            // Send connection request to server
            if (n == 3) {
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "Please enter a different name.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
                continue;
            }
            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
            // Await server response
            char server_reply[BUFFER_SIZE];
            udp_socket_read(args->sd, &args->server_addr, server_reply, BUFFER_SIZE);
            
            if (strcmp(server_reply, "ok") ==0) {
                break; // Exit initial loop
            }
            // Server did not accept name
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "Please enter a different name.\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
        }
        else {
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "Invalid connection command. Use: conn$ YourName\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
        }
    }
    // Set connected = true
    pthread_mutex_lock(&args->mutex);
    args->connected = true;
    pthread_mutex_unlock(&args->mutex);
    // Final confirmation message
    pthread_mutex_lock(&ncurses_mutex);
    wprintw(win_output, "Connected to server.\n");
    wrefresh(win_output);
    pthread_mutex_unlock(&ncurses_mutex);
}

// sender_thread
// This thread handles user input after the client is connected.
// Some commands change variables in client
//     conn$NAME     - reconnect after disconnection
// All other input is forwarded directly to server.
// sender_thread always runs, even if disconnected. It checks connected state
// and warns the user if they type something while disconnected.
void *sender_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE], req_type[BUFFER_SIZE], req_cont[BUFFER_SIZE];
    // Main message loop
    while (1) {
        // --- Display prompt ---
        pthread_mutex_lock(&ncurses_mutex);
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 1, ">> ");
        wmove(win_input, 1, 4);
        wrefresh(win_input);
        echo();
        pthread_mutex_unlock(&ncurses_mutex);

        // Get user input
        wgetnstr(win_input, client_request, BUFFER_SIZE - 1);

        pthread_mutex_lock(&ncurses_mutex);
        noecho();
        pthread_mutex_unlock(&ncurses_mutex);

        client_request[strcspn(client_request, "\n")] = '\0';
        sscanf(client_request, "%[^$]$ %[^\n]", req_type, req_cont);

        // Thread-safe read of connection state
        pthread_mutex_lock(&args->mutex);
        bool is_connected = args->connected;
        pthread_mutex_unlock(&args->mutex);

        // Handle disconnected state
        if (!is_connected && (!(strcmp(req_type, "conn") == 0))) {
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "You are disconnected\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
            continue;
        }
        // Command: conn$ (reconnection)
        if (strcmp(req_type, "conn") == 0) {
            pthread_mutex_lock(&args->mutex);
            if (!args->connected) {
                args->connected = true; //trusts that the server has the client's info stored
                udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);

                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "Reconnected.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
            }
            pthread_mutex_unlock(&args->mutex);

            continue;
        }
        // All other commands/messages is forwarded to server
        else {
            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
        }
    }
    return NULL;
}

// listener_thread
// Continuously receives UDP packets from the server.
// Special messages from server trigger forced disconnection:
//   "You have been removed from the chat"
//   "You have been disconnected from the chat due to inactivity"
// Injection cannot happen since the message sent by other clients would be
// in the form "name: message" so it cannot match the special messages
void *listener_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    struct sockaddr_in responder_addr;
    
    char server_response[BUFFER_SIZE];

    while (1) {
        int rc = udp_socket_read(args->sd, &responder_addr, server_response, BUFFER_SIZE);
        if (rc > 0) {
            server_response[rc] = '\0';
            // Read connection state safely
            pthread_mutex_lock(&args->mutex);
            bool is_connected = args->connected;
            pthread_mutex_unlock(&args->mutex);

            // Display incoming message if connected
            if (is_connected) {
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "%s\n", server_response);
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
            }

            // Disconnect message by server (kick or inactivity or reply of disconn$)
            if ((strcmp(server_response,"You have been removed from the chat") == 0)|| (strcmp(server_response, "You have been disconnected from the chat due to inactivity") == 0)|| (strcmp(server_response, "Disconnected. Bye!") == 0)) {
                pthread_mutex_lock(&args->mutex);
                args->connected = false;
                pthread_mutex_unlock(&args->mutex);
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "Disconnected. Type conn$ to reconnect.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
                strcpy(server_response, "disconnect");
            }
        }
    }
    return NULL;
}

// main()
// Sets up socket, initializes ncurses UI, and starts the three threads.
int main(int argc, char *argv[]) {
    int port = 0;  // default to random available port

    // special admin mode if needed  (conn$ admin)
    if (argc >= 2) {
        if (strcmp(argv[1], "admin") == 0) {
            port = 6666;
        }
    }

    // Create UDP socket
    int sd = udp_socket_open(port);
    assert(sd > -1);

    // Connect to server IP/port
    struct sockaddr_in server_addr;
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    // Initialize thread arguments
    thread_args_t args = { 
        .sd = sd,
        .server_addr = server_addr,
        .connected = false
    };
    pthread_mutex_init(&args.mutex, NULL);

    // Initialize ncurses UI
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    #define INPUT_HEIGHT 3

    win_output = newwin(rows - INPUT_HEIGHT, cols, 0, 0);
    win_input  = newwin(INPUT_HEIGHT, cols, rows - INPUT_HEIGHT, 0);

    scrollok(win_output, TRUE);

    box(win_input, 0, 0);
    wrefresh(win_input);
    wrefresh(win_output);

    // ---- Thread creation ----
    pthread_t sender_tid, listener_tid, init_tid;

    // First, run the initial connection loop
    pthread_create(&init_tid, NULL, initial_thread, &args);
    pthread_join(init_tid, NULL);

    // After connection, start sender + listener
    pthread_create(&sender_tid, NULL, sender_thread, &args);
    pthread_create(&listener_tid, NULL, listener_thread, &args);

    pthread_join(sender_tid, NULL);
    pthread_join(listener_tid, NULL);

    // Cleanup
    close(sd);
    endwin();
    return 0;
}
