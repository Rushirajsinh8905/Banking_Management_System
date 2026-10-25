#include "employee.h"
#include "auth.h"
#include "common.h"
#include "records.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void handle_employee(int sock, int eid) {
    char buffer[BUFFER_SIZE];

    for (;;) {
        send_message(sock,
            "\n=== Employee Menu ===\n"
            "1. Add New Customer\n"
            "2. Modify Customer Details\n"
            "3. Process Loan Applications\n"
            "4. View Assigned Loan Applications\n"
            "5. View Customer Transactions (Passbook)\n"
            "6. Change Password\n"
            "7. Close Customer Account\n"
            "8. Logout\n"
            "Choice: ");

        if (!read_input(sock, buffer, BUFFER_SIZE)) return;
        int choice;
        if (!parse_int(buffer, &choice)) choice = -1;

        switch (choice) {
            case 1: add_customer(sock); break;
            case 2: modify_account_details(sock, ROLE_CUSTOMER); break;
            case 3: process_loan(sock, eid); break;
            case 4: view_assigned_loans(sock, eid); break;
            case 5: view_customer_transactions(sock); break;
            case 6: change_password(sock, ROLE_EMPLOYEE, eid); break;
            case 7: close_account(sock); break;
            case 8: return;
            default: send_message(sock, "Invalid choice\n");
        }
    }
}


static void add_customer(int sock) {
    char buf[BUFFER_SIZE];
    char username[FIELD_MAX], password[FIELD_MAX];
    double balance;

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

    send_message(sock, "Initial balance: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_amount(buf, &balance, 1)) {
        send_message(sock, "Invalid balance\n");
        return;
    }

    int new_id = -1;
    const char *err = NULL;

    db_wrlock(DB_CUSTOMERS);
    if (find_account_by_username(ROLE_CUSTOMER, username, NULL)) {
        err = "Username already exists\n";
    } else {
        new_id = get_next_id_csv(CUSTOMERS_CSV);
        AccountRecord r = { .id = new_id, .balance = balance, .active = ACCOUNT_ACTIVE };
        safe_copy(r.username, username, sizeof(r.username));
        safe_copy(r.password, password, sizeof(r.password));
        char acc_line[256];
        format_account_line(ROLE_CUSTOMER, acc_line, sizeof(acc_line), &r);
        if (append_line(CUSTOMERS_CSV, acc_line) != 0)      /* open + write + fsync */
            err = "Error saving customer\n";
    }
    db_unlock(DB_CUSTOMERS);

    if (err) { send_message(sock, err); return; }
    if (balance > 0) log_transaction(new_id, "OPENING_DEPOSIT", balance, balance);

    snprintf(buf, sizeof(buf), "Customer created! ID: %d\n", new_id);
    send_message(sock, buf);
}

static void view_assigned_loans(int sock, int eid) {
    char emp_id_str[20];
    snprintf(emp_id_str, sizeof(emp_id_str), "%d", eid);

    StrBuf out;
    sb_init(&out);
    int count = 0;
    sb_appendf(&out, "\n=== Assigned Loans ===\n");

    db_rdlock(DB_LOANS);
    LineReader lr;
    if (lr_open(&lr, LOANS_CSV) == 0) {
        char line[300];
        if (lr_next(&lr, line, sizeof(line))) {
            while (lr_next(&lr, line, sizeof(line))) {
                LoanRecord l;
                trim_string(line);
                if (parse_loan_line(line, &l) && strcmp(l.assigned, emp_id_str) == 0) {
                    sb_appendf(&out, "Loan ID: %d | Customer: %d | Amount: %.2f | Type: %s | "
                                     "Tenure: %dy | Status: %s\n",
                               l.loan_id, l.customer_id, l.amount, l.type, l.tenure, l.status);
                    count++;
                }
            }
        }
        lr_close(&lr);
    }
    db_unlock(DB_LOANS);

    if (count == 0) sb_appendf(&out, "No loans assigned to you\n");
    if (out.data) send_message(sock, out.data);
    sb_free(&out);
}

