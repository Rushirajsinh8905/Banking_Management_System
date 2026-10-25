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


/* Caller holds db_wrlock(DB_CUSTOMERS). Returns 0 on success. */
static int credit_customer(int cid, double amount, double *new_balance) {
    CsvRewrite rw;
    if (csv_rewrite_begin(&rw, CUSTOMERS_CSV, role_tmp_path(ROLE_CUSTOMER)) != 0) return -1;

    char line[256];
    int found = 0;
    if (!lr_next(&rw.in, line, sizeof(line))) line[0] = '\0';
    sb_puts(&rw.out, role_csv_header(ROLE_CUSTOMER));

    while (lr_next(&rw.in, line, sizeof(line))) {
        AccountRecord r;
        trim_string(line);
        if (!parse_account_line(ROLE_CUSTOMER, line, &r)) {
            if (line[0]) sb_appendf(&rw.out, "%s\n", line);
            continue;
        }
        if (r.id == cid && r.active == ACCOUNT_ACTIVE) {   /* never credit a deactivated/closed account */
            r.balance += amount;
            *new_balance = r.balance;
            found = 1;
        }
        write_account_line(ROLE_CUSTOMER, &rw.out, &r);
    }

    if (!found) { csv_rewrite_abort(&rw); return -1; }
    return csv_rewrite_commit(&rw);
}

/* FIXES vs. old version:
 *  - loans.csv and customers.csv were rewritten with NO locking at all
 *  - an already approved loan could be approved again -> money credited twice
 *  - a "DEBUG: ..." line was sent to the client
 *  - if crediting failed, the loan stayed "approved" anyway
 * Now: lock loans -> customers (lock order), build the new loans file, credit
 * the customer, and only if that worked rename the new loans file in. */
static void process_loan(int sock, int eid) {
    char buf[BUFFER_SIZE];
    int lid, approve;

    send_message(sock, "Enter loan ID: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &lid) || lid <= 0) { send_message(sock, "Invalid loan ID\n"); return; }

    send_message(sock, "Approve (1) or Reject (0): ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &approve) || (approve != 0 && approve != 1)) {
        send_message(sock, "Please enter 1 or 0\n");
        return;
    }

    char emp_id_str[20];
    snprintf(emp_id_str, sizeof(emp_id_str), "%d", eid);

    enum { NOT_FOUND, NOT_YOURS, NOT_PENDING, DONE, CREDIT_FAIL, IO_FAIL, CREDIT_LATE_FAIL } status = NOT_FOUND;
    LoanRecord target;
    double new_balance = 0;
    CsvRewrite rw;

    db_wrlock(DB_LOANS);
    if (csv_rewrite_begin(&rw, LOANS_CSV, LOANS_TMP) != 0) {
        db_unlock(DB_LOANS);
        send_message(sock, "Error opening loans file\n");
        return;
    }

    char line[300];
    if (!lr_next(&rw.in, line, sizeof(line))) line[0] = '\0';
    sb_puts(&rw.out, LOANS_HEADER);

    while (lr_next(&rw.in, line, sizeof(line))) {
        LoanRecord l;
        trim_string(line);
        if (!parse_loan_line(line, &l)) {
            if (line[0]) sb_appendf(&rw.out, "%s\n", line);
            continue;
        }
        if (l.loan_id == lid) {
            if (strcmp(l.assigned, emp_id_str) != 0)   status = NOT_YOURS;
            else if (strcmp(l.status, "pending") != 0) status = NOT_PENDING;
            else {
                safe_copy(l.status, approve ? "approved" : "rejected", sizeof(l.status));
                target = l;
                status = DONE;
            }
        }
        write_loan_line(&rw.out, &l);
    }

    if (status != DONE) {
        csv_rewrite_abort(&rw);
    } else if (approve) {
        /* Two files must change (loans.csv and customers.csv) and a crash can
         * only stop us BETWEEN them. Order chosen so that the worst case never
         * creates money:  1) check the customer can receive money,
         * 2) save "approved" in loans.csv, 3) credit the customer.
         * A crash after step 2 leaves "approved, not credited" (visible and
         * fixable); the old order could credit twice. */
        AccountRecord cust;
        db_wrlock(DB_CUSTOMERS);
        if (!find_account_by_id(ROLE_CUSTOMER, target.customer_id, &cust) ||
            cust.active != ACCOUNT_ACTIVE) {
            status = CREDIT_FAIL;
            csv_rewrite_abort(&rw);                 /* keep loan pending */
        } else if (csv_rewrite_commit(&rw) != 0) {  /* step 2 */
            status = IO_FAIL;
        } else if (credit_customer(target.customer_id, target.amount, &new_balance) != 0) {
            status = CREDIT_LATE_FAIL;              /* step 3 failed */
            log_err("[ALERT] loan %d approved but crediting customer %d FAILED\n",
                    target.loan_id, target.customer_id);
        } else {
            log_transaction(target.customer_id, "LOAN_CREDIT", target.amount, new_balance);
        }
        db_unlock(DB_CUSTOMERS);
    } else if (csv_rewrite_commit(&rw) != 0) {
        status = IO_FAIL;
    }
    db_unlock(DB_LOANS);

    switch (status) {
        case DONE:
            if (approve) {
                snprintf(buf, sizeof(buf),
                         "Loan approved! Amount %.2f credited to customer %d's account\n",
                         target.amount, target.customer_id);
                send_message(sock, buf);
            } else {
                send_message(sock, "Loan rejected\n");
            }
            break;
        case NOT_YOURS:   send_message(sock, "This loan is not assigned to you\n"); break;
        case NOT_PENDING: send_message(sock, "This loan has already been processed\n"); break;
        case CREDIT_FAIL: send_message(sock, "Customer account is missing, deactivated or closed; loan left pending\n"); break;
        case CREDIT_LATE_FAIL: send_message(sock, "Loan saved as approved but crediting the account failed - contact the administrator\n"); break;
        case IO_FAIL:     send_message(sock, "Error saving loan file\n"); break;
        default:          send_message(sock, "Loan not found\n"); break;
    }
}


static void view_customer_transactions(int sock) {
    char buf[BUFFER_SIZE];
    int cid;

    send_message(sock, "Enter customer ID: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &cid) || cid <= 0) { send_message(sock, "Invalid ID\n"); return; }

    StrBuf out;
    sb_init(&out);
    int count = 0;
    sb_appendf(&out, "\n=== Customer Transaction History (Passbook) ===\n");

    db_rdlock(DB_TRANSACTIONS);
    LineReader lr;
    if (lr_open(&lr, TRANSACTIONS_CSV) == 0) {
        char line[256];
        if (lr_next(&lr, line, sizeof(line))) {
            while (lr_next(&lr, line, sizeof(line))) {
                int customer_id;
                char type[20], timestamp[50];
                double amount, balance_after;
                trim_string(line);
                if (sscanf(line, "%d,%19[^,],%lf,%lf,%49[^\n]",
                           &customer_id, type, &amount, &balance_after, timestamp) == 5 &&
                    customer_id == cid) {
                    sb_appendf(&out, "%s: %.2f | Balance: %.2f | %s\n",
                               type, amount, balance_after, timestamp);
                    count++;
                }
            }
        }
        lr_close(&lr);
    }
    db_unlock(DB_TRANSACTIONS);

    if (count == 0) sb_appendf(&out, "No transactions found for this customer\n");
    if (out.data) send_message(sock, out.data);
    sb_free(&out);
}

/* Closes a customer account (PDF: employee "opening, modifying, or closing
 * accounts"). The row is NOT deleted: it is marked ACCOUNT_CLOSED, so the
 * customer id can never be reused and the passbook history stays valid.
 * Rule: the balance must be 0.00 (money never disappears). */
static void close_account(int sock) {
    char buf[BUFFER_SIZE];
    int cid;

    send_message(sock, "Enter customer ID to close: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (!parse_int(buf, &cid) || cid <= 0) { send_message(sock, "Invalid ID\n"); return; }

    send_message(sock, "Type YES to permanently close this account: ");
    if (!read_input(sock, buf, BUFFER_SIZE)) return;
    if (strcmp(buf, "YES") != 0) { send_message(sock, "Cancelled\n"); return; }

    enum { C_NOT_FOUND, C_ALREADY, C_HAS_BALANCE, C_DONE, C_IO } status = C_NOT_FOUND;
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
            if (r.active == ACCOUNT_CLOSED)  status = C_ALREADY;
            else if (r.balance > 0.004)      status = C_HAS_BALANCE;
            else { r.active = ACCOUNT_CLOSED; status = C_DONE; }
        }
        write_account_line(ROLE_CUSTOMER, &rw.out, &r);
    }

    if (status == C_DONE) {
        if (csv_rewrite_commit(&rw) != 0) status = C_IO;
        else log_transaction(cid, "ACCOUNT_CLOSED", 0.0, 0.0);
    } else {
        csv_rewrite_abort(&rw);
    }
    db_unlock(DB_CUSTOMERS);

    switch (status) {
        case C_DONE:        send_message(sock, "Account closed\n"); break;
        case C_ALREADY:     send_message(sock, "Account is already closed\n"); break;
        case C_HAS_BALANCE: send_message(sock, "Balance must be 0.00 before closing (customer must withdraw or transfer it first)\n"); break;
        case C_IO:          send_message(sock, "Error saving changes\n"); break;
        default:            send_message(sock, "Account not found\n"); break;
    }
}
