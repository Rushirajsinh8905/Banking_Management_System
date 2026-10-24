#include "records.h"

#include <stdio.h>
#include <string.h>

int role_is_valid(int role) {
    return role >= ROLE_CUSTOMER && role <= ROLE_ADMIN;
}

const char *role_name(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return "Customer";
        case ROLE_EMPLOYEE: return "Employee";
        case ROLE_MANAGER:  return "Manager";
        case ROLE_ADMIN:    return "Admin";
        default:            return "Unknown";
    }
}

DbFile role_db(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return DB_CUSTOMERS;
        case ROLE_EMPLOYEE: return DB_EMPLOYEES;
        case ROLE_MANAGER:  return DB_MANAGERS;
        default:            return DB_ADMINS;
    }
}

/* String literals -> immutable -> thread-safe (the old get_csv_path() used a
 * shared static buffer that two threads could overwrite at the same time). */
const char *role_csv_path(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return CUSTOMERS_CSV;
        case ROLE_EMPLOYEE: return EMPLOYEES_CSV;
        case ROLE_MANAGER:  return MANAGERS_CSV;
        default:            return ADMINS_CSV;
    }
}

const char *role_tmp_path(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return DATA_DIR "/customers.tmp";
        case ROLE_EMPLOYEE: return DATA_DIR "/employees.tmp";
        case ROLE_MANAGER:  return DATA_DIR "/managers.tmp";
        default:            return DATA_DIR "/admins.tmp";
    }
}

const char *role_csv_header(int role) {
    return role == ROLE_CUSTOMER ? "id,username,password,balance,active\n"
                                 : "id,username,password\n";
}

int parse_account_line(int role, const char *line, AccountRecord *r) {
    memset(r, 0, sizeof(*r));

    if (role == ROLE_CUSTOMER) {
        if (sscanf(line, "%d,%49[^,],%49[^,],%lf,%d",
                   &r->id, r->username, r->password, &r->balance, &r->active) != 5)
            return 0;
    } else {
        if (sscanf(line, "%d,%49[^,],%49[^\n]", &r->id, r->username, r->password) != 3)
            return 0;
        r->active = 1;
    }
    trim_string(r->username);
    trim_string(r->password);
    return 1;
}

void format_account_line(int role, char *buf, size_t size, const AccountRecord *r) {
    if (role == ROLE_CUSTOMER)
        snprintf(buf, size, "%d,%s,%s,%.2f,%d\n", r->id, r->username, r->password, r->balance, r->active);
    else
        snprintf(buf, size, "%d,%s,%s\n", r->id, r->username, r->password);
}

void write_account_line(int role, StrBuf *out, const AccountRecord *r) {
    char buf[256];
    format_account_line(role, buf, sizeof(buf), r);
    sb_puts(out, buf);
}
static int find_account(int role, int id, const char *username, AccountRecord *out) {
    LineReader lr;
    if (lr_open(&lr, role_csv_path(role)) != 0) return 0;

    char line[256];
    int found = 0;

    if (lr_next(&lr, line, sizeof(line))) {         /* skip header */
        while (lr_next(&lr, line, sizeof(line))) {
            AccountRecord r;
            trim_string(line);
            if (!parse_account_line(role, line, &r)) continue;
            if ((username && strcmp(r.username, username) == 0) || (!username && r.id == id)) {
                if (out) *out = r;
                found = 1;
                break;
            }
        }
    }
    lr_close(&lr);
    return found;
}
int find_account_by_id(int role, int id, AccountRecord *out) {
    return find_account(role, id, NULL, out);
}

int find_account_by_username(int role, const char *username, AccountRecord *out) {
    return find_account(role, 0, username, out);
}
