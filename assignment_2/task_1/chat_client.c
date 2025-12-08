#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <ncurses.h>
#include "udp.h"

#define CLIENT_PORT 0   // Random available port

typedef struct {
    int sd;
    struct sockaddr_in server_addr;
    bool connected;
    pthread_mutex_t *mutex;
} thread_args_t;

// ncurses windows and mutex for thread-safe updates
WINDOW *win_input, *win_output;
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

void *sender_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char client_request[BUFFER_SIZE], req_type[BUFFER_SIZE], req_cont[BUFFER_SIZE];

    // Initial connection loop
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
            pthread_mutex_lock(args->mutex);
            if (!args->connected) {
                args->connected = true;   // mark as reconnected
                udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);
                pthread_mutex_lock(&ncurses_mutex);
                wprintw(win_output, "Reconnected to the server.\n");
                wrefresh(win_output);
                pthread_mutex_unlock(&ncurses_mutex);
            } 
            pthread_mutex_unlock(args->mutex);
            break;
        }

        pthread_mutex_lock(&ncurses_mutex);
        wprintw(win_output, "Invalid connection command. Use: conn$ YourName\n");
        wrefresh(win_output);
        pthread_mutex_unlock(&ncurses_mutex);
    }

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
        udp_socket_write(args->sd, &args->server_addr, client_request, BUFFER_SIZE);

        sscanf(client_request, "%[^$]$ %[^\n]", req_type, req_cont);
        if (strcmp(req_type, "disconn") == 0) {
            pthread_mutex_lock(args->mutex);
            args->connected = false;
            pthread_mutex_unlock(args->mutex);
            
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "You are now disconnected. Type conn$ to reconnect.\n");
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);
            continue;
        }
    }
    return NULL;
}

void *listener_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    struct sockaddr_in responder_addr;
    char server_response[BUFFER_SIZE];

    // Wait for initial connection
    while (1) {
        pthread_mutex_lock(args->mutex);
        bool is_connected = args->connected;
        pthread_mutex_unlock(args->mutex);

        if (is_connected) break;
        usleep(10000);
    }

    while (1) {
        int rc = udp_socket_read(args->sd, &responder_addr, server_response, BUFFER_SIZE);
        pthread_mutex_lock(args->mutex);
        bool is_connected = args->connected;
        pthread_mutex_unlock(args->mutex);
        if (!is_connected) {
            // Skip processing server messages while disconnected
            continue;
        }
        if (rc > 0) {
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(win_output, "%s\n", server_response);
            wrefresh(win_output);
            pthread_mutex_unlock(&ncurses_mutex);

            pthread_mutex_lock(args->mutex);
            bool is_connected = args->connected;
            pthread_mutex_unlock(args->mutex);

            if (!is_connected) break;
        } else if (rc == -1) {
            pthread_mutex_lock(args->mutex);
            args->connected = false;
            pthread_mutex_unlock(args->mutex);
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int sd = udp_socket_open(CLIENT_PORT);
    assert(sd > -1);

    struct sockaddr_in server_addr;
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    assert(rc == 0);

    pthread_mutex_t access_mutex = PTHREAD_MUTEX_INITIALIZER;
    thread_args_t args = { .sd = sd, .server_addr = server_addr, .connected = false, .mutex = &access_mutex };

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

    pthread_t sender_tid, listener_tid;
    pthread_create(&sender_tid, NULL, sender_thread, &args);
    pthread_create(&listener_tid, NULL, listener_thread, &args);

    pthread_join(sender_tid, NULL);
    pthread_join(listener_tid, NULL);

    close(sd);
    endwin(); // End ncurses
    return 0;
}
