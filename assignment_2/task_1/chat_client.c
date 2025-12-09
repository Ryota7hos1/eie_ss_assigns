#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <ncurses.h>
#include "udp.h"

typedef struct {
    int sd;
    struct sockaddr_in server_addr;
    bool connected;
    pthread_mutex_t mutex;
} thread_args_t;

// ncurses windows and mutex for thread-safe updates
WINDOW *win_input, *win_output;
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

void *initial_thread(void *arg) { //initial loop
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE], req_type[BUFFER_SIZE], req_cont[BUFFER_SIZE];
    while (1) {
        pthread_mutex_lock(&ncurses_mutex);
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 1, ">> ");
        wmove(win_input, 1, 4);  // Move cursor after prompt
        wrefresh(win_input);
        echo(); // Show typed characters
        pthread_mutex_unlock(&ncurses_mutex);

        wgetnstr(win_input, client_request, BUFFER_SIZE - 1);

        pthread_mutex_lock(&ncurses_mutex);
        noecho(); // Stop echoing
        pthread_mutex_unlock(&ncurses_mutex);

        client_request[strcspn(client_request, "\n")] = '\0';
        sscanf(client_request, "%[^$]$ %[^\n]", req_type, req_cont);

        if (strcmp(req_type, "conn") == 0) {
            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "I wrote to the server.\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
            char server_reply[BUFFER_SIZE];
            udp_socket_read(args->sd, &args->server_addr, server_reply, BUFFER_SIZE);
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "%s\n", server_reply);
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
            if (strcmp(server_reply, "ok") ==0) {
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "I'm getting an ok.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
                break;
            }
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "I'm don't get an ok.\n");
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
    pthread_mutex_lock(&args->mutex);
    args->connected = true;
    pthread_mutex_unlock(&args->mutex);
            
    pthread_mutex_lock(&ncurses_mutex);
    wprintw(win_output, "Connected to server.\n");
    wrefresh(win_output);
    pthread_mutex_unlock(&ncurses_mutex);
}

void *sender_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE], req_type[BUFFER_SIZE], req_cont[BUFFER_SIZE];
    // Main message loop
    while (1) {
        pthread_mutex_lock(&ncurses_mutex);
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 1, ">> ");
        wmove(win_input, 1, 4);
        wrefresh(win_input);
        echo();
        pthread_mutex_unlock(&ncurses_mutex);

        wgetnstr(win_input, client_request, BUFFER_SIZE - 1);

        pthread_mutex_lock(&ncurses_mutex);
        noecho();
        pthread_mutex_unlock(&ncurses_mutex);
        client_request[strcspn(client_request, "\n")] = '\0';
        sscanf(client_request, "%[^$]$ %[^\n]", req_type, req_cont);
        pthread_mutex_lock(&args->mutex);
        bool is_connected = args->connected;
        pthread_mutex_unlock(&args->mutex);
        if (!is_connected) {
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "You are disconnected\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
        }
        else if (strcmp(req_type, "disconn") == 0) {
            pthread_mutex_lock(&args->mutex);
            args->connected = false;
            pthread_mutex_unlock(&args->mutex);

            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);

            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "Disconnected. Type conn$ to reconnect.\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
            continue; // Stay in input loop
        }
        if (strcmp(req_type, "conn") == 0) {
            pthread_mutex_lock(&args->mutex);
            if (!args->connected) {
                args->connected = true;
                udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);

                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "Reconnected.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
            }
            pthread_mutex_unlock(&args->mutex);

            continue;
        }
        else {
            udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
        }
    }
    return NULL;
}

void *listener_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    struct sockaddr_in responder_addr;
    char server_response[BUFFER_SIZE];

    while (1) {
        int rc = udp_socket_read(args->sd, &responder_addr, server_response, BUFFER_SIZE);
        if (rc > 0) {
            server_response[rc] = '\0';

            pthread_mutex_lock(&args->mutex);
            bool is_connected = args->connected;
            pthread_mutex_unlock(&args->mutex);

            // Always read packets, but only display if connected
            if (is_connected) {
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "%s\n", server_response);
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
            }

            if ((strcmp(server_response,"You have been removed from the chat") == 0)|| (strcmp(server_response, "You have been disconnected from the chat due to inactivity") == 0)) {
                pthread_mutex_lock(&args->mutex);
                args->connected = false;
                pthread_mutex_unlock(&args->mutex);
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "disconnected\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
                strcpy(server_response, "disconnect");
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 0;  // default to random available port

    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using random port.\n");
            port = 0;
        }
    }
    int sd = udp_socket_open(port);
    assert(sd > -1);

    struct sockaddr_in server_addr;
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    thread_args_t args = { 
        .sd = sd,
        .server_addr = server_addr,
        .connected = false
    };
    pthread_mutex_init(&args.mutex, NULL);

    // Initialize ncurses
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

    pthread_t sender_tid, listener_tid, init_tid;
    pthread_create(&init_tid, NULL, initial_thread, &args);
    pthread_join(init_tid, NULL);
    pthread_create(&sender_tid, NULL, sender_thread, &args);
    pthread_create(&listener_tid, NULL, listener_thread, &args);

    pthread_join(sender_tid, NULL);
    pthread_join(listener_tid, NULL);

    close(sd);
    endwin(); // End ncurses
    return 0;
}
