#include "utils.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Linux: MSG_NOSIGNAL stops send() from raising SIGPIPE.
 * macOS has no MSG_NOSIGNAL; there we use SO_NOSIGPIPE on the socket and
 * also ignore SIGPIPE process-wide (see signals.c). */
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/* ------------------------------------------------------------------------- */
/* Per-file read/write locks                                                 */
/* ------------------------------------------------------------------------- */
static pthread_rwlock_t db_locks[DB_COUNT] = {
    PTHREAD_RWLOCK_INITIALIZER, PTHREAD_RWLOCK_INITIALIZER,
    PTHREAD_RWLOCK_INITIALIZER, PTHREAD_RWLOCK_INITIALIZER,
    PTHREAD_RWLOCK_INITIALIZER, PTHREAD_RWLOCK_INITIALIZER,
    PTHREAD_RWLOCK_INITIALIZER
};

void db_rdlock(DbFile f) { pthread_rwlock_rdlock(&db_locks[f]); }
void db_wrlock(DbFile f) { pthread_rwlock_wrlock(&db_locks[f]); }
void db_unlock(DbFile f) { pthread_rwlock_unlock(&db_locks[f]); }

/* ------------------------------------------------------------------------- */
/* Socket I/O                                                                */
/* ------------------------------------------------------------------------- */

/* Sends the whole message. Handles partial writes and EINTR. If the peer has
 * gone away, send() fails with EPIPE (instead of killing the process with
 * SIGPIPE) and we simply return. */
void send_message(int sock, const char *msg) {
    size_t len = strlen(msg);
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(sock, msg + sent, len - sent, SEND_FLAGS);
        if (n < 0) {
            if (errno == EINTR) continue;   /* interrupted by a signal: retry */
            return;                         /* EPIPE / ECONNRESET: client gone */
        }
        sent += (size_t)n;
    }
}

/* Reads one line (terminated by '\n') from the socket.
 *  - returns 1 on success (buffer holds the trimmed line, always NUL-terminated)
 *  - returns 0 if the client disconnected, the server is shutting down
 *    (socket was shutdown()), or the idle timeout (SO_RCVTIMEO) expired.
 * Reading byte-by-byte guarantees that two lines typed quickly are never
 * merged into one read and that we never overflow `buffer`. */
int read_input(int sock, char *buffer, int size) {
    int len = 0;

    if (size <= 0) return 0;
    buffer[0] = '\0';

    for (;;) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);

        if (n == 0) return 0;                       /* orderly EOF */
        if (n < 0) {
            if (errno == EINTR) continue;           /* signal interrupted recv */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                send_message(sock,
                    "\n\n*** Session timed out due to inactivity. Disconnecting. ***\n");
            }
            return 0;
        }

        if (c == '\n') break;
        if (c == '\r') continue;                    /* telnet / nc send CRLF */
        if (len < size - 1) buffer[len++] = c;      /* silently drop overflow */
    }

    buffer[len] = '\0';
    trim_string(buffer);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* fcntl advisory locking                                                    */
/* ------------------------------------------------------------------------- */
int lock_file(int fd, int lock_type) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = (short)lock_type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;                                 /* whole file */

    while (fcntl(fd, F_SETLKW, &lock) == -1) {
        if (errno == EINTR) continue;               /* wait was interrupted */
        log_err("Error acquiring lock: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int unlock_file(int fd) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        log_err("Error releasing lock: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* write_all / logging / LineReader (system calls only)                      */
/* ------------------------------------------------------------------------- */
int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, p + done, len - done);
        if (w == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)w;
    }
    return 0;
}

static void log_fd(int fd, const char *fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    write_all(fd, buf, (size_t)n);
}

void log_out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fd(STDOUT_FILENO, fmt, ap);
    va_end(ap);
}

void log_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fd(STDERR_FILENO, fmt, ap);
    va_end(ap);
}
