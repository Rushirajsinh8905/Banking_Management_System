#include "customer.h"
#include "auth.h"
#include "common.h"
#include "records.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Rule followed everywhere below: take the lock, touch the file, release the
 * lock, THEN talk to the client. Never block on the network with a lock held. */


void handle_customer(int sock, int cid) {
    char buffer[BUFFER_SIZE];

    for (;;) {
        send_message(sock,
            "\n=== Customer Menu ===\n"
            "1. View Account Balance\n"
            "2. Deposit Money\n"
            "3. Withdraw Money\n"
            "4. Transfer Funds\n"
            "5. Apply for a Loan\n"
            "6. Check Loan Status\n"
            "7. Change Password\n"
            "8. Add Feedback\n"
            "9. View Transaction History\n"
            "10. Logout\n"
            "Choice: ");

        if (!read_input(sock, buffer, BUFFER_SIZE)) return;   /* disconnect */
        int choice;
        if (!parse_int(buffer, &choice)) choice = -1;

        double amount;
        switch (choice) {
            case 1: view_balance(sock, cid); break;
            case 2:
            case 3:
                send_message(sock, "Enter amount: ");
                if (!read_input(sock, buffer, BUFFER_SIZE)) return;
                /* FIX: negative / zero / non-numeric amounts used to be
                 * accepted -> "deposit -500" was a withdrawal that skipped
                 * the balance check. */
                if (!parse_amount(buffer, &amount, 0)) {
                    send_message(sock, "Invalid amount (must be a positive number)\n");
                    break;
                }
                update_balance(sock, cid, amount, choice == 2);
                break;
            case 4: transfer_funds(sock, cid); break;
            case 5: apply_loan(sock, cid); break;
            case 6: check_loan_status(sock, cid); break;
            case 7: change_password(sock, ROLE_CUSTOMER, cid); break;
            case 8: add_feedback(sock, cid); break;
            case 9: view_transactions(sock, cid); break;
            case 10: return;
            default: send_message(sock, "Invalid choice\n");
        }
    }
}


static void view_balance(int sock, int cid) {
    AccountRecord r;
    char msg[100];

    db_rdlock(DB_CUSTOMERS);
    int found = find_account_by_id(ROLE_CUSTOMER, cid, &r);
    db_unlock(DB_CUSTOMERS);

    if (found) snprintf(msg, sizeof(msg), "Balance: %.2f\n", r.balance);
    else       snprintf(msg, sizeof(msg), "Error: account not found\n");
    send_message(sock, msg);
}


/* status codes for balance operations */
enum { OP_NOT_FOUND = 0, OP_OK, OP_INSUFFICIENT, OP_INACTIVE, OP_IO_ERROR,
       OP_NO_RECIPIENT, OP_RECIPIENT_INACTIVE };

static void update_balance(int sock, int cid, double amount, int is_deposit) {
    int status = OP_NOT_FOUND;
    double new_balance = 0;
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
            if (line[0]) sb_appendf(&rw.out, "%s\n", line);  /* never drop unknown rows */
            continue;
        }
        if (r.id == cid) {
            if (r.active != ACCOUNT_ACTIVE) { status = OP_INACTIVE; break; }
            if (is_deposit) {
                r.balance += amount;
            } else if (r.balance < amount) {
                status = OP_INSUFFICIENT;
                break;
            } else {
                r.balance -= amount;
            }
            new_balance = r.balance;
            status = OP_OK;
        }
        write_account_line(ROLE_CUSTOMER, &rw.out, &r);
    }

    if (status == OP_OK) {
        if (csv_rewrite_commit(&rw) == 0)
            log_transaction(cid, is_deposit ? "DEPOSIT" : "WITHDRAWAL", amount, new_balance);
        else
            status = OP_IO_ERROR;
    } else {
        csv_rewrite_abort(&rw);
    }
    db_unlock(DB_CUSTOMERS);

    char msg[BUFFER_SIZE];
    switch (status) {
        case OP_OK:
            snprintf(msg, sizeof(msg), "Success! New balance: %.2f\n", new_balance); break;
        case OP_INSUFFICIENT: safe_copy(msg, "Insufficient balance\n", sizeof(msg)); break;
        case OP_INACTIVE:     safe_copy(msg, "Account is deactivated or closed\n", sizeof(msg)); break;
        case OP_IO_ERROR:     safe_copy(msg, "Error saving changes\n", sizeof(msg)); break;
        default:              safe_copy(msg, "Error: account not found\n", sizeof(msg)); break;
    }
    send_message(sock, msg);
}
