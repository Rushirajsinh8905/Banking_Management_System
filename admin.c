#include "admin.h"
#include "auth.h"
#include "common.h"
#include "records.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_employee(int sock) {
    char buf[BUFFER_SIZE];
    char username[FIELD_MAX], password[FIELD_MAX];

    send_message(sock, "Enter username: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!is_valid_field(buf)) {
        send_message(sock, "Invalid username (1-49 characters, no commas)\n");
        return;
    }
    safe_copy(username, buf, sizeof(username));

    send_message(sock, "Enter password: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!is_valid_field(buf)) {
        send_message(sock, "Invalid password (1-49 characters, no commas)\n");
        return;
    }
    safe_copy(password, buf, sizeof(password));

    int new_id = -1;
    const char *err = NULL;

    db_wrlock(DB_EMPLOYEES);
    if (find_account_by_username(ROLE_EMPLOYEE, username, NULL)) {
        err = "Username already exists\n";
    } else {
        new_id = get_next_id_csv(EMPLOYEES_CSV);
        AccountRecord r = { .id = new_id };
        safe_copy(r.username, username, sizeof(r.username));
        safe_copy(r.password, password, sizeof(r.password));
        char acc_line[256];
        format_account_line(ROLE_EMPLOYEE, acc_line, sizeof(acc_line), &r);
        if (append_line(EMPLOYEES_CSV, acc_line) != 0)      /* open + write + fsync */
            err = "Error saving employee\n";
    }
    db_unlock(DB_EMPLOYEES);

    if (err) { send_message(sock, err); return; }
    snprintf(buf, sizeof(buf), "Employee created! ID: %d\n", new_id);
    send_message(sock, buf);
}


void handle_admin(int sock, int aid) {
    char buffer[BUFFER_SIZE];

    for (;;) {
        send_message(sock,
            "\n=== Admin Menu ===\n"
            "1. Add New Bank Employee\n"
            "2. Modify Customer Details\n"
            "3. Modify Employee Details\n"
            "4. Manage User Roles\n"
            "5. Change Password\n"
            "6. Logout\n"
            "Choice: ");

        if (!read_input(sock, buffer, BUFFER_SIZE)) return;
        int choice;
        if (!parse_int(buffer, &choice)) choice = -1;

        switch (choice) {
            case 1: add_employee(sock); break;
            case 2: modify_account_details(sock, ROLE_CUSTOMER); break;
            case 3: modify_account_details(sock, ROLE_EMPLOYEE); break;
            case 4: manage_user_roles(sock); break;
            case 5: change_password(sock, ROLE_ADMIN, aid); break;
            case 6: return;
            default: send_message(sock, "Invalid choice\n");
        }
    }
}
