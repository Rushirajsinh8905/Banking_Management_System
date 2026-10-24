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


/* Reads everything from fd (starting at the current offset) into memory. */
static int read_fd_all(int fd, char **out, size_t *len) {
    size_t cap = 4096, n = 0;
    char *b = malloc(cap);
    if (!b) return -1;

    for (;;) {
        if (n + 1 >= cap) {
            cap *= 2;
            char *t = realloc(b, cap);
            if (!t) { free(b); return -1; }
            b = t;
        }
        ssize_t r = read(fd, b + n, cap - n - 1);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(b);
            return -1;
        }
        if (r == 0) break;
        n += (size_t)r;
    }
    b[n] = '\0';
    *out = b;
    *len = n;
    return 0;
}

int lr_from_fd(LineReader *lr, int fd) {
    memset(lr, 0, sizeof(*lr));
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) return -1;
    return read_fd_all(fd, &lr->data, &lr->len);
}

int lr_open(LineReader *lr, const char *path) {
    memset(lr, 0, sizeof(*lr));
    int fd = open(path, O_RDONLY);
    if (fd == -1) return -1;
    int rc = read_fd_all(fd, &lr->data, &lr->len);
    close(fd);
    return rc;
}

/* Copies the next line (without '\n') into `line`; lines longer than the
 * buffer are truncated. Returns 1 if a line was returned, 0 at end of file. */
int lr_next(LineReader *lr, char *line, size_t size) {
    if (!lr->data || lr->pos >= lr->len || size == 0) return 0;

    size_t i = lr->pos;
    while (i < lr->len && lr->data[i] != '\n') i++;

    size_t n = i - lr->pos;
    if (n >= size) n = size - 1;
    memcpy(line, lr->data + lr->pos, n);
    line[n] = '\0';

    lr->pos = (i < lr->len) ? i + 1 : i;
    return 1;
}

void lr_close(LineReader *lr) {
    free(lr->data);
    memset(lr, 0, sizeof(*lr));
}


/* ------------------------------------------------------------------------- */
/* Misc helpers                                                              */
/* ------------------------------------------------------------------------- */

/* Trims whitespace on both ends, in place. */
void trim_string(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }

    size_t start = 0;
    while (str[start] == ' ' || str[start] == '\t') start++;
    if (start > 0) memmove(str, str + start, len - start + 1);
}


/* Bounded copy that always NUL-terminates (replaces unsafe strcpy). */
void safe_copy(char *dst, const char *src, size_t size) {
    if (size == 0) return;
    snprintf(dst, size, "%s", src);
}

/* A username/password must be non-empty, fit in FIELD_MAX, and must not
 * contain characters that would corrupt the CSV format. */
int is_valid_field(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len >= FIELD_MAX) return 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == ',' || s[i] == '\n' || s[i] == '\r') return 0;
    }
    return 1;
}

/* Strict number parsing: rejects "abc", "12abc", "nan", "inf", negatives. */
int parse_amount(const char *s, double *out, int allow_zero) {
    char *end;
    errno = 0;
    double v = strtod(s, &end);

    if (end == s || errno != 0) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return 0;
    if (!isfinite(v)) return 0;
    if (allow_zero ? (v < 0) : (v <= 0)) return 0;
    if (v > MAX_AMOUNT) return 0;

    *out = v;
    return 1;
}

int parse_int(const char *s, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);

    if (end == s || errno != 0) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return 0;
    if (v < INT_MIN || v > INT_MAX) return 0;

    *out = (int)v;
    return 1;
}

/* localtime() uses a static buffer and is NOT thread-safe; localtime_r is. */
void current_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_now);
}

void sanitize_free_text(char *s) {
    for (; *s; s++) {
        if (*s == ',') *s = ';';
        else if (*s == '\n' || *s == '\r') *s = ' ';
    }
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
/* CSV rewrite: build new content in memory, then write+fsync+rename         */
/* ------------------------------------------------------------------------- */
int csv_rewrite_begin(CsvRewrite *rw, const char *path, const char *tmp_path) {
    memset(rw, 0, sizeof(*rw));
    rw->fd = -1;
    rw->path = path;
    rw->tmp = tmp_path;
    sb_init(&rw->out);

    rw->fd = open(path, O_RDWR);
    if (rw->fd == -1) return -1;

    if (lock_file(rw->fd, F_WRLCK) == -1) {
        close(rw->fd);
        return -1;
    }

    if (lr_from_fd(&rw->in, rw->fd) != 0) {
        unlock_file(rw->fd);
        close(rw->fd);
        return -1;
    }
    return 0;
}

int csv_rewrite_commit(CsvRewrite *rw) {
    int ok = 1;

    lr_close(&rw->in);

    int tfd = open(rw->tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd == -1) {
        ok = 0;
    } else {
        if (rw->out.len > 0 && write_all(tfd, rw->out.data, rw->out.len) != 0) ok = 0;
        if (ok && fsync(tfd) != 0) ok = 0;          /* data really on disk */
        if (close(tfd) != 0) ok = 0;
    }

    /* rename() is atomic: readers see either the old or the new file, never
     * a half-written one - even if the server is killed right here. */
    if (ok && rename(rw->tmp, rw->path) != 0) {
        log_err("rename: %s\n", strerror(errno));
        ok = 0;
    }
    if (ok) fsync_parent_dir(rw->path);
    if (!ok) unlink(rw->tmp);

    sb_free(&rw->out);
    unlock_file(rw->fd);
    close(rw->fd);
    return ok ? 0 : -1;
}

void csv_rewrite_abort(CsvRewrite *rw) {
    lr_close(&rw->in);
    sb_free(&rw->out);
    unlock_file(rw->fd);
    close(rw->fd);
}


/* ------------------------------------------------------------------------- */
/* String buffer                                                             */
/* ------------------------------------------------------------------------- */
void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

int sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (need < 0) { va_end(ap2); return -1; }

    if (sb->len + (size_t)need + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap : 256;
        while (ncap < sb->len + (size_t)need + 1) ncap *= 2;
        char *p = realloc(sb->data, ncap);
        if (!p) { va_end(ap2); return -1; }
        sb->data = p;
        sb->cap = ncap;
    }

    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)need;
    return 0;
}

int sb_puts(StrBuf *sb, const char *s) {
    return sb_appendf(sb, "%s", s);
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}



int append_line(const char *path, const char *line) {
    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd == -1) return -1;

    int rc = write_all(fd, line, strlen(line));
    if (rc == 0) rc = fsync(fd);                /* data really on disk before we say "ok" */
    close(fd);
    return rc;
}

/* The caller (deposit/withdraw/transfer/loan credit) holds the customers lock
 * while calling this, so the balance change and its passbook line are done in
 * one locked section. Lock order: CUSTOMERS < TRANSACTIONS (see utils.h). */
int log_transaction(int cid, const char *type, double amt, double balance_after) {
    char ts[32], line[160];
    current_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line), "%d,%s,%.2f,%.2f,%s\n", cid, type, amt, balance_after, ts);

    db_wrlock(DB_TRANSACTIONS);
    int rc = append_line(DATA_DIR "/transactions.csv", line);
    db_unlock(DB_TRANSACTIONS);

    if (rc != 0)
        log_err("[ALERT] passbook write FAILED: customer %d %s %.2f (balance already changed)\n",
                cid, type, amt);
    return rc;
}


/* Caller must hold the write lock of the file, otherwise two threads could
 * compute the same "next id". */
int get_next_id_csv(const char *csv_path) {
    LineReader lr;
    if (lr_open(&lr, csv_path) != 0) return 1;

    char line[512];
    int max_id = 0;

    if (lr_next(&lr, line, sizeof(line))) {         /* skip header */
        while (lr_next(&lr, line, sizeof(line))) {
            int id;
            if (sscanf(line, "%d,", &id) == 1 && id > max_id) max_id = id;
        }
    }

    lr_close(&lr);
    return max_id + 1;
}


/* fsync() the directory that holds `path`, so the rename() itself survives a
 * power failure (the file data was already fsync'ed). Best effort. */
static void fsync_parent_dir(const char *path) {
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else snprintf(dir, sizeof(dir), ".");

    int dfd = open(dir, O_RDONLY);
    if (dfd == -1) return;
    fsync(dfd);
    close(dfd);
}
