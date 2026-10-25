#include "manager.h"
#include "auth.h"
#include "common.h"
#include "records.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void activate_account(int sock) {
    char buf[BUFFER_SIZE];
    int cid, activate;

    send_message(sock, "Enter customer ID: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &cid) || cid <= 0) { send_message(sock, "Invalid ID\n"); return; }

    send_message(sock, "Activate (1) or Deactivate (0): ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &activate) || (activate != 0 && activate != 1)) {
        send_message(sock, "Please enter 1 or 0\n");
        return;
    }

    int found = 0, closed = 0, ok = 1;
    CsvRewrite rw;

    db_wrlock(DB_CUSTOMERS);
    if (csv_rewrite_begin(&rw, CUSTOMERS_CSV, role_tmp_path(ROLE_CUSTOMER)) != 0) {
        db_unlock(DB_CUSTOMERS);
        send_message(sock, "Error opening file\n");
        return;
    }

    char line[256];
    if (!lr_next(&rw.in, line, sizeof(line))) line[0] = '\0';
    sb_puts(&rw.out, role_csv_header(ROLE_CUSTOMER));

    while (lr_next(&rw.in, line, sizeof(line))) {
        AccountRecord r;
        trim_string(line);
        if (!parse_account_line(ROLE_CUSTOMER, line, &r)) {
            if (line[0]) sb_appendf(&rw.out, "%s\n", line);
            continue;
        }
        if (r.id == cid) {
            if (r.active == ACCOUNT_CLOSED) {
                closed = 1;                 /* closed accounts stay closed */
            } else {
                r.active = activate ? ACCOUNT_ACTIVE : ACCOUNT_DEACTIVATED;
                found = 1;
            }
        }
        write_account_line(ROLE_CUSTOMER, &rw.out, &r);
    }

    if (found) ok = (csv_rewrite_commit(&rw) == 0);
    else csv_rewrite_abort(&rw);
    db_unlock(DB_CUSTOMERS);

    if (!ok)        send_message(sock, "Error saving changes\n");
    else if (closed) send_message(sock, "Account is closed and cannot be changed\n");
    else if (found) send_message(sock, activate ? "Account activated\n" : "Account deactivated\n");
    else            send_message(sock, "Account not found\n");
}


void handle_manager(int sock, int mid) {
    char buffer[BUFFER_SIZE];

    for (;;) {
        send_message(sock,
            "\n=== Manager Menu ===\n"
            "1. Activate/Deactivate Customer Accounts\n"
            "2. Assign Loan Application Processes to Employees\n"
            "3. Review Customer Feedback\n"
            "4. Change Password\n"
            "5. Logout\n"
            "Choice: ");

        if (!read_input(sock, buffer, BUFFER_SIZE)) return;
        int choice;
        if (!parse_int(buffer, &choice)) choice = -1;

        switch (choice) {
            case 1: activate_account(sock); break;
            case 2: assign_loan(sock); break;
            case 3: view_feedback(sock); break;
            case 4: change_password(sock, ROLE_MANAGER, mid); break;
            case 5: return;
            default: send_message(sock, "Invalid choice\n");
        }
    }
}
