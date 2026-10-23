#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * In-process locks for every CSV "table".
 *
 * WHY: fcntl() record locks belong to the *process*, not the thread. All client
 * threads live in one server process, so fcntl() alone never blocks one thread
 * against another. A pthread read-write lock per file gives real mutual
 * exclusion between threads (many readers OR one writer).
 *
 * LOCK ORDER (always acquire in this order to avoid deadlock):
 *   LOANS < CUSTOMERS < EMPLOYEES < MANAGERS < ADMINS < TRANSACTIONS < FEEDBACK
 * ------------------------------------------------------------------------- */
typedef enum {
    DB_LOANS = 0,
    DB_CUSTOMERS,
    DB_EMPLOYEES,
    DB_MANAGERS,
    DB_ADMINS,
    DB_TRANSACTIONS,
    DB_FEEDBACK,
    DB_COUNT
} DbFile;

void db_rdlock(DbFile f);
void db_wrlock(DbFile f);
void db_unlock(DbFile f);

/* Socket I/O (EINTR-safe, SIGPIPE-safe, line based) */
void send_message(int sock, const char *msg);
int  read_input(int sock, char *buffer, int size);

/* fcntl advisory locks (protect against *other processes*) */
int lock_file(int fd, int lock_type);
int unlock_file(int fd);

/* Growable string buffer: build output while holding a lock, send it later */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
int  sb_appendf(StrBuf *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int  sb_puts(StrBuf *sb, const char *s);
void sb_free(StrBuf *sb);

/* ---------------------------------------------------------------------------
 * File I/O layer: ONLY system calls (open/read/write/fsync/rename/unlink),
 * no FILE*, fopen, fgets or fprintf anywhere in the project.
 * ------------------------------------------------------------------------- */

/* write() the whole buffer (retries on EINTR / partial writes). 0 = ok */
int  write_all(int fd, const void *buf, size_t len);

/* Console logging through write(2): one write() per message, thread-safe. */
void log_out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* stdout */
void log_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* stderr */

/* Reads a whole file with open()+read() into memory and hands it out line by
 * line (a replacement for fopen()+fgets()).                                  */
typedef struct {
    char  *data;       /* malloc'd copy of the file, NUL-terminated */
    size_t len;
    size_t pos;
} LineReader;

int  lr_open(LineReader *lr, const char *path);   /* 0 = ok, -1 = cannot open  */
int  lr_from_fd(LineReader *lr, int fd);          /* read an already open fd   */
int  lr_next(LineReader *lr, char *line, size_t size);  /* 1 = line, 0 = end   */
void lr_close(LineReader *lr);

/* Safe "rewrite a CSV" helper (replaces the old FILE*-based version).
 *   begin  : open(O_RDWR) + fcntl write lock + read the old file into memory
 *   work   : caller reads old lines with lr_next(&rw.in, ...) and builds the
 *            new content with sb_appendf(&rw.out, ...) / sb_puts(&rw.out, ...)
 *   commit : write() new content to a temp file, fsync(), rename() over the
 *            old file (atomic), fsync() the directory, unlock.
 * Nothing touches the disk before commit, so an abort or a crash before commit
 * leaves the original file untouched. Caller holds the matching db_wrlock(). */
typedef struct {
    int         fd;
    LineReader  in;
    StrBuf      out;
    const char *path;
    const char *tmp;
} CsvRewrite;

int  csv_rewrite_begin(CsvRewrite *rw, const char *path, const char *tmp_path);
int  csv_rewrite_commit(CsvRewrite *rw);   /* 0 on success (atomic rename) */
void csv_rewrite_abort(CsvRewrite *rw);    /* discard                       */

/* Misc helpers */
int  get_next_id_csv(const char *csv_path);
void trim_string(char *str);
void safe_copy(char *dst, const char *src, size_t size);
int  is_valid_field(const char *s);           /* non-empty, < FIELD_MAX, no commas */
void sanitize_free_text(char *s);             /* commas -> ';' for free text      */
int  parse_amount(const char *s, double *out, int allow_zero);
int  parse_int(const char *s, int *out);
void current_timestamp(char *buf, size_t size);
int  log_transaction(int cid, const char *type, double amt, double balance_after);  /* 0 = ok */

/* Append one line to a file using system calls only:
 * open(O_APPEND) + write() + fsync() + close(). Returns 0 on success.
 * Caller holds the matching db_wrlock(). */
int  append_line(const char *path, const char *line);

#endif
