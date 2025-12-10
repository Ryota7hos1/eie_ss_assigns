// ---------------------------------------------------------------------
// udp.h   —   Utility Header for UDP Socket Operations and Core Data
// Structures (Client Records, Block Lists, Circular Buffers)
//
// This header provides:
//   • UDP socket helper functions (open, read, write, address setup)
//   • Core data structures used by the server:
//        - Node               : represents a client
//        - BlockNode          : linked list for mute functionality
//        - CircularBuffer     : global and per-client message history
//   • Linked list utilities for managing clients
//   • Circular buffer utilities for message history
//
// This file is shared by the server and client components.
// ---------------------------------------------------------------------

// libraries needed for various functions
#include <sys/types.h>  // data types like size_t, socklen_t
#include <sys/socket.h> // socket(), bind(), connect(), listen(), accept()
#include <netinet/in.h> // sockaddr_in, htons(), htonl(), INADDR_ANY
#include <arpa/inet.h>  // inet_pton(), inet_ntop()
#include <unistd.h>     // close()
#include <string.h>     // memset(), memcpy()
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>

///global constants
#define BUFFER_SIZE 1024
#define SERVER_PORT 12000
#define CB_SIZE 15   ///circular buffer size

int set_socket_addr(struct sockaddr_in *addr, const char *ip, int port)
{
    // This is a helper function that fills
    // data into an address variable of the
    // type struct sockaddr_in

    // Basic initialization (sin stands for 'socket internet')
    memset(addr, 0, sizeof(*addr)); // zero the whole memory
    addr->sin_family = AF_INET;     // use IPv4
    addr->sin_port = htons(port);   // host-to-network short: required to store port in network byte order

    if (ip == NULL)
    { // special behaviour for using IP 0.0.0.0 (INADDR_ANY)
        addr->sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        // Convert dotted-decimal string (e.g., "127.0.0.1") to binary.
        // In other words: inet_pton will parse the human-readable IP string
        // and write the corresponding 32-bit binary value into sin_addr
        if (inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
        {
            return -1; // invalid IP string
        }
    }
    return 0;
}

int udp_socket_open(int port)
{

    // 1. Create a UDP socket and obtain a socket descriptor
    // (sd) to it
    int sd = socket(AF_INET, SOCK_DGRAM, 0);

    // 2. Create an address variable to associate
    // (or bind) with the socket
    struct sockaddr_in this_addr;

    // 3. Fill in the address variable with IP and Port.
    // Having ip = NULL (second parameter below), sets IP to 0.0.0.0
    // which represents all network interfaces (IP addresses)
    // for this machine.
    set_socket_addr(&this_addr, NULL, port);

    // 4. Bind (associate) this address with the socket
    // created earlier.
    /// Note: binding with 0.0.0.0 means that the socket will accept
    // packets coming in to any interface (ip address) of this machine
    bind(sd, (struct sockaddr *)&this_addr, sizeof(this_addr));

    return sd; // return the socket descriptor
}

int udp_socket_read(int sd, struct sockaddr_in *addr, char *buffer, int n)
{
    // Receive up to n bytes into buffer from the socket with descriptor sd.
    // The source address (addr) of the sender is stored in addr for later use.

    // Note: recvfrom is a blocking call, meaning that the function will not return
    // until a packet arrives (or an error/timeout occurs). During this time, the
    // calling thread is put to sleep by the kernel and placed on a wait queue, and
    // it resumes only when recvfrom completes.

    // The fourth parameter 'flags' of recvfrom is normally set to 0

    socklen_t len = sizeof(struct sockaddr_in);
    return recvfrom(sd, buffer, n, 0, (struct sockaddr *)addr, &len);
}

int udp_socket_write(int sd, struct sockaddr_in *addr, char *buffer, int n)
{
    // Send the contents of buffer (n bytes) to the given destination
    // address (addr) over UDP (through socket with descriptor sd)

    // addr_len: tells the kernel how many bytes of addr are valid
    // (needed to distinguish IPv4 vs IPv6 etc.)

    // Note: sendto may also block if the kernel’s send buffer is full, which usually
    // happens only under high load or network pressure. In that case, the function will
    // not return until buffer space becomes available (or an error/timeout occurs).
    // During this time, the calling thread is put to sleep by the kernel and placed on
    // a wait queue, and it resumes only when sendto completes.

    // The fourth parameter 'flags' of sendto is normally set to 0

    int addr_len = sizeof(struct sockaddr_in);
    return sendto(sd, buffer, n, 0, (struct sockaddr *)addr, addr_len);
}

// =====================================================================
//                CLIENT DATA STRUCTURES (SERVER-SIDE)
// =====================================================================

// Forward declarations (needed because BlockNode stores Node*, and Node
// stores BlockNode*)
typedef struct Node Node;
typedef struct BlockNode BlockNode;

/*
 * BlockNode:
 * ------------------------------------------------------------
 * Used to implement mute functionality.
 * For each client X, X.blocked_by is a linked list of BlockNodes,
 * each storing a pointer to a client who muted X.
 */
struct BlockNode {
    Node *client; /// store Nodes
    BlockNode *next; /// Linked list structure
};

/*
 * Node:
 * ------------------------------------------------------------
 * Represents a single client in the server's linked list.
 *
 * Fields:
 *   name         - client's username
 *   client_ad    - client's socket address (IP + port)
 *   next         - next client in linked list
 *   blocked_by   - linked list of clients who muted this one
 *
 *   last_active  - timestamp for inactivity detection
 *   connected    - supports reconnection (state preserved)
 *
 *   history[][]  - circular buffer for private or global history (server only)
 *   hist_head    - oldest element index
 *   hist_count   - number of stored messages
 *   history_lock - protects circular buffer
 */
struct Node {
    char name[BUFFER_SIZE]; 
    struct sockaddr_in client_ad; 
    Node *next;
    BlockNode *blocked_by;
    time_t last_active; 
    bool connected;  
    char history[CB_SIZE][BUFFER_SIZE];  
    int hist_head;
    int hist_count;
    pthread_mutex_t history_lock;
};

/*
 * Packet:
 * ------------------------------------------------------------
 * A worker thread receives one of these from the listener.
 * Contains:
 *    • socket descriptor
 *    • client's address
 *    • raw message buffer
 */
typedef struct {
    int sd;
    struct sockaddr_in client_addr;
    char message[BUFFER_SIZE];
} Packet;

// =====================================================================
//                      CIRCULAR BUFFER STRUCT
// =====================================================================

/*
 * CircularBuffer:
 * ------------------------------------------------------------
 * Generic circular buffer for storing last CB_SIZE messages.
 * Used for:
 *    - Global history (server node)
 *    - Private history (client nodes)
 *
 * head       → index of oldest message
 * count      → number of messages currently stored
 * lock       → protects modifications
 * not_empty  → condition variable for blocking pops
 */
typedef struct {
    char data[CB_SIZE][BUFFER_SIZE];
    int head;   
    int count;   

    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} CircularBuffer;

// =====================================================================
//                      LINKED LIST OPERATIONS
// =====================================================================

/*
 * create_node:
 * ------------------------------------------------------------
 * Allocates and initializes a new client Node.
 * Sets:
 *    - name
 *    - address
 *    - time stamp
 *    - history buffer
 */
Node* create_node(const char *name, struct sockaddr_in client_ad) {
    Node *n = malloc(sizeof(Node));
    strncpy(n->name, name, BUFFER_SIZE);
    n->name[BUFFER_SIZE-1] = '\0';
    n->client_ad = client_ad;
    n->connected = true;
    n->last_active = time(NULL);
    n->hist_head = 0;
    n->hist_count = 0;
    n->next = NULL;
    n->blocked_by = NULL;
    pthread_mutex_init(&n->history_lock, NULL);
    return n;
}

/*
 * push_back:
 * ------------------------------------------------------------
 * Appends a new Node at the end of the linked list.
 * Server node is always at the head.
 */
void push_back(Node **head, const char *name, struct sockaddr_in client_ad) {
    Node *n = create_node(name, client_ad);

    if (*head == NULL) {
        *head = n;
        return;
    }

    Node *cur = *head;
    while (cur->next != NULL) {
        cur = cur->next;
    }
    cur->next = n;
}

/*
 * find_node:
 * ------------------------------------------------------------
 * Search for a client by username.
 */
Node* find_node(Node *head, const char *target_name) {
    Node *cur = head;
    while (cur != NULL) {
        if (strncmp(cur->name, target_name, BUFFER_SIZE) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/*
 * find_node_addr:
 * ------------------------------------------------------------
 * Search for a client by socket address.
 */
Node* find_node_addr(Node *head, struct sockaddr_in client_ad) {
    Node *cur = head;
    while (cur != NULL) {
        if (cur->client_ad.sin_family == client_ad.sin_family &&
            cur->client_ad.sin_port == client_ad.sin_port &&
            cur->client_ad.sin_addr.s_addr == client_ad.sin_addr.s_addr)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/*
 * free_blocklist:
 * ------------------------------------------------------------
 * Frees the linked list of BlockNodes.
 * (Not used anymore because disconnection simply flips a boolean
 * for client data preservation.)
 */
void free_blocklist(BlockNode* head) {
    BlockNode* cur = head;
    while (cur != NULL) {
        BlockNode* tmp = cur;
        cur = cur->next;
        free(tmp);
    }
}

/*
 * disconnect_node:
 * ------------------------------------------------------------
 * Old deletion logic replaced.
 * Now simply marks a client as disconnected while preserving its
 * history, mute list, and node in the list.
 */
void disconnect_node(Node** head, Node* target) {
    target->connected = false;
}


/*
 * create_blocknode:
 * ------------------------------------------------------------
 * Allocates a BlockNode (not used, push_back_blocknode creates
 * these inline).
 */
BlockNode* create_blocknode(Node* target) {
    BlockNode *n = malloc(sizeof(BlockNode));
    n->client = target;
    n->next = NULL;
    return n;
}

/*
 * push_back_blocknode:
 * ------------------------------------------------------------
 * Adds a BlockNode to the beginning of `blocked->blocked_by`.
 * Meaning: blocker has muted blocked.
 */
void push_back_blocknode(Node *blocker, Node* blocked) {
    BlockNode *n = malloc(sizeof(BlockNode));
    n->client = blocker;
    n->next = blocked->blocked_by;
    blocked->blocked_by = n;
}

/*
 * remove_blocknode:
 * ------------------------------------------------------------
 * Removes 'blocker' from 'blocked->blocked_by' list.
 */
void remove_blocknode(Node *blocker, Node* blocked) {
    BlockNode *cur = blocked->blocked_by;
    BlockNode *prev = NULL;
    while (cur != NULL) {
        if (cur->client == blocker) {
            // Remove from list
            if (prev == NULL) {
                blocked->blocked_by = cur->next;
            } else {
                prev->next = cur->next;
            }
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}


// =====================================================================
//                      CIRCULAR BUFFER OPERATIONS
// =====================================================================

/*
 * cb_init:
 * ------------------------------------------------------------
 * Initializes a CircularBuffer.
 * Not used directly because global history became the server's
 * node history buffer.
 */
void cb_init(CircularBuffer *cb) {
    cb->head = 0;
    cb->count = 0;

    pthread_mutex_init(&cb->lock, NULL);
    pthread_cond_init(&cb->not_empty, NULL);
}

/*
 * cb_destroy:
 * ------------------------------------------------------------
 * Destroys locks/conditions associated with buffer.
 * Not required under current design.
 */
void cb_destroy(CircularBuffer *cb) {
    pthread_mutex_destroy(&cb->lock);
    pthread_cond_destroy(&cb->not_empty);
}

/*
 * cb_pop:
 * ------------------------------------------------------------
 * Blocking pop — waits until there is at least one message.
 */
int cb_pop(CircularBuffer *cb, char *out) {
    pthread_mutex_lock(&cb->lock);

    // Wait for data
    while (cb->count == 0) {
        pthread_cond_wait(&cb->not_empty, &cb->lock);
    }

    strncpy(out, cb->data[cb->head], BUFFER_SIZE);
    cb->head = (cb->head + 1) % CB_SIZE;
    cb->count--;

    pthread_mutex_unlock(&cb->lock);
    return 1;
}

/*
 * cb_iterate:
 * ------------------------------------------------------------
 * Iterates through current entries in a circular buffer and calls
 * a user-provided callback function for each message.
 * (Used by server to send history to new clients.)
 */
void cb_iterate(CircularBuffer *cb, void (*callback)(const char *msg)) {
    pthread_mutex_lock(&cb->lock);

    for (int i = 0; i < cb->count; i++) {
        int idx = (cb->head + i) % CB_SIZE;
        callback(cb->data[idx]);
    }

    pthread_mutex_unlock(&cb->lock);
}


/*
 * node_cb_push:
 * ------------------------------------------------------------
 * Pushes message `msg` into the node's private (or global) history.
 * Overwrites oldest messages when buffer is full.
 */
void node_cb_push(Node* node, const char* msg) {
    if (!node) return;
    pthread_mutex_lock(&node->history_lock);
    int idx = (node->hist_head + node->hist_count) % CB_SIZE;
    strncpy(node->history[idx], msg, BUFFER_SIZE);
    node->history[idx][BUFFER_SIZE-1] = '\0';

    if (node->hist_count < CB_SIZE) {
        node->hist_count++;
    } else {
        // Buffer is full, advance head
        node->hist_head = (node->hist_head + 1) % CB_SIZE;
    }
    pthread_mutex_unlock(&node->history_lock);
}