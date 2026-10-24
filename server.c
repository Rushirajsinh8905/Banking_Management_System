#include "common.h"
#include "admin.h"
#include "auth.h"
#include "customer.h"
#include "employee.h"
#include "manager.h"
#include "records.h"
#include "server.h"
#include "session_manager.h"
#include "signals.h"
#include "utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Registry of connected client sockets. Needed so that on shutdown the main
 * thread can tell every client "server is going down" and shutdown() their
 * sockets, which makes each worker thread's blocking recv() return 0.
 * ------------------------------------------------------------------------- */
static int client_fds[MAX_CLIENTS];
static int active_clients = 0;
static pthread_mutex_t clients_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  clients_cv  = PTHREAD_COND_INITIALIZER;
static time_t start_time;


typedef struct {
    int  sock;
    char peer[64];
} ClientArg;


static int register_client(int fd) {
    int ok = 0;
    pthread_mutex_lock(&clients_mtx);
    if (active_clients < MAX_CLIENTS) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == -1) {
                client_fds[i] = fd;
                active_clients++;
                ok = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mtx);
    return ok;
}


/* Must be called BEFORE close(fd), so the main thread never calls shutdown()
 * on an fd number that has already been closed and reused. */
static void unregister_client(int fd) {
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == fd) {
            client_fds[i] = -1;
            active_clients--;
            break;
        }
    }
    pthread_cond_signal(&clients_cv);       /* wake shutdown waiter */
    pthread_mutex_unlock(&clients_mtx);
}

/* ------------------------------------------------------------------------- */
/* One thread per client                                                     */
/* ------------------------------------------------------------------------- */
static void *handle_client(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int sock = ca->sock;
    char peer[64];
    safe_copy(peer, ca->peer, sizeof(peer));
    free(ca);

    char buffer[BUFFER_SIZE];

    while (server_running()) {
        send_message(sock,
            "\n=== Banking System ===\n"
            "1. Customer\n"
            "2. Employee\n"
            "3. Manager\n"
            "4. Admin\n"
            "5. Exit\n"
            "Select role: ");

        if (!read_input(sock, buffer, BUFFER_SIZE)) break;

        int role;
        if (!parse_int(buffer, &role)) role = -1;

        if (role == 5) {
            send_message(sock, "Goodbye!\n");
            break;
        }
        if (!role_is_valid(role)) {
            send_message(sock, "Invalid role!\n");
            continue;
        }

        /* FIX: strcpy(username, buffer) copied up to 1023 bytes into a
         * 50-byte stack array -> stack buffer overflow. Now bounded. */
        char username[FIELD_MAX], password[FIELD_MAX];

        send_message(sock, "Username: ");
        if (!read_input(sock, buffer, BUFFER_SIZE)) break;
        safe_copy(username, buffer, sizeof(username));

        send_message(sock, "Password: ");
        if (!read_input(sock, buffer, BUFFER_SIZE)) break;
        safe_copy(password, buffer, sizeof(password));

        int user_id = -1;
        int rc = authenticate(role, username, password, &user_id);

        if (rc == AUTH_ALREADY_LOGGED_IN) {
            send_message(sock, "\n✗ This account is already logged in from another session.\n");
            continue;
        }
        if (rc == AUTH_CLOSED) {
            send_message(sock, "\n✗ This account has been closed. Please contact the bank.\n");
            continue;
        }
        if (rc == AUTH_INACTIVE) {
            send_message(sock, "\n✗ Account is deactivated. Please contact the bank.\n");
            continue;
        }
        if (rc != AUTH_OK) {
            send_message(sock, "\n✗ Authentication failed!\n");
            continue;
        }

        log_out("[auth] %s '%s' (id=%d) logged in from %s\n",
               role_name(role), username, user_id, peer);
        send_message(sock, "\n✓ Login successful!\n");

        switch (role) {
            case ROLE_CUSTOMER: handle_customer(sock, user_id); break;
            case ROLE_EMPLOYEE: handle_employee(sock, user_id); break;
            case ROLE_MANAGER:  handle_manager(sock, user_id);  break;
            case ROLE_ADMIN:    handle_admin(sock, user_id);    break;
        }

        /* Reached on normal logout, on client disconnect, on idle timeout
         * and on server shutdown - so the session slot is always freed. */
        logout_user(role, user_id);
        log_out("[auth] %s '%s' (id=%d) logged out\n", role_name(role), username, user_id);
        send_message(sock, "\nLogged out.\n");
    }

    log_out("[-] Client %s disconnected\n", peer);
    unregister_client(sock);
    close(sock);
    return NULL;
}
