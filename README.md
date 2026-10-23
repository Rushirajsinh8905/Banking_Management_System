# Banking Management System (CS-513 System Software mini project)

> **New to Linux?** Follow the step-by-step [LINUX_RUN_GUIDE.md](LINUX_RUN_GUIDE.md): install, build, run, log in,
> test, reset data, common problems and a presentation demo script.

A multi-client TCP banking server written in C. Four roles (Customer, Employee,
Manager, Admin) log in through a text client. The server keeps all data in CSV
files and uses **only system calls** for files, sockets, locking, shared memory,
semaphores and console output (no `fopen`/`FILE*`/`fgets`/`printf`, and no `fork`).

## 1. Build and run

```bash
make                 # builds ./server and ./client (0 warnings)
./server             # listens on port 8080   (or: ./server 9000)
./client             # in another terminal    (or: ./client 127.0.0.1 9000)
```

Stop the server with **Ctrl+C** (graceful). Print live status with
`kill -USR1 <server-pid>`. If the server was killed with `kill -9`, run
`make reset-ipc` (or just start it again: it re-uses the stale shared memory).
`make setup` resets all data files to the sample data (**overwrites everything**).

Default logins (password `pass123`, except where shown):

| Role | Username |
|---|---|
| Customer | `customer1`, `customer2` (balance 2000), `alice` (`alice123`), `bob` (`bob123`) |
| Employee | `employee1` |
| Manager | `manager1` |
| Admin | `admin1` |

## 2. Menus (what each role can do)

| Role | Menu |
|---|---|
| Customer | 1 View balance, 2 Deposit, 3 Withdraw, 4 Transfer, 5 Apply for loan, 6 Check loan status, 7 Change password, 8 Add feedback, 9 Transaction history, 10 Logout |
| Employee | 1 Add customer, 2 Modify customer, 3 Process loan (approve/reject), 4 View assigned loans, 5 Customer passbook, 6 Change password, 7 **Close customer account**, 8 Logout |
| Manager | 1 Activate/deactivate customer, 2 Assign loan to employee, 3 Review feedback, 4 Change password, 5 Logout |
| Admin | 1 Add employee, 2 Modify customer, 3 Modify employee, 4 Manage roles (promote/demote), 5 Change password, 6 Logout |

"Exit" is option **5** of the first (role) menu. One session per user: a second
login with the same account is refused until the first one logs out or disconnects.

Account states (`active` column of `customers.csv`): `1` active, `0` deactivated
(manager can switch it back on), `2` closed by an employee (permanent; the balance
must be 0.00; the row is kept so ids are never reused and the passbook stays valid).

## 3. Source map

| File | Purpose |
|---|---|
| `server.c` | accept loop, one POSIX thread per client, role menu, graceful shutdown |
| `signals.c` | one thread receives SIGINT/SIGTERM/SIGQUIT/SIGHUP/SIGUSR1 with `sigwait()` |
| `session_manager.c` | who is logged in: System V shared memory + System V semaphore |
| `auth.c` | `authenticate`, `change_password`, `modify_account_details` |
| `customer.c`, `employee.c`, `manager.c`, `admin.c` | the four role menus |
| `records.c` | one place that knows the CSV layout of every file |
| `utils.c` | locks, socket I/O, `write_all`, `append_line`, `LineReader`, `CsvRewrite`, logging |
| `client.c` | terminal client (`poll()` on socket + keyboard, Ctrl+C handling) |
| `class_diagram.pdf` | class diagram that matches this code |

## 4. How concurrency and consistency work (plain words)

* **Many clients:** the server creates one thread per connected client (max 100).
* **Threads must not collide:** every CSV file has a `pthread_rwlock_t` (many readers
  *or* one writer). A fixed lock order (`LOANS < CUSTOMERS < EMPLOYEES < MANAGERS < ADMINS < TRANSACTIONS < FEEDBACK`)
  prevents deadlocks. **Locks are never held while talking to the network.**
* **`fcntl` file lock:** taken on the file during every rewrite (`csv_rewrite_begin`).
  `fcntl` locks belong to the *process*, so they do not separate two threads of the
  same server - the pthread locks do that. The `fcntl` lock protects the file against a
  second *process* touching it.
* **One session per user:** the session table lives in shared memory and is protected
  by a System V semaphore (`semop`). 30 simultaneous logins of one account: exactly one wins.
* **Atomic file update:** a rewrite builds the new file in memory, `write()`s it to a
  temp file, `fsync()`s, `rename()`s it over the old file (atomic) and `fsync()`s the
  directory. Readers see the old or the new file, never half. A crash before the
  rename leaves the old file untouched.
* **Transfer:** both balances are in the same file, so one `rename()` updates both:
  money is never debited without being credited.
* **Appends** (new loan, feedback, passbook row, new account) use `open(O_APPEND)` +
  `write()` + `fsync()`.

### ACID summary (honest version)

| Property | Status |
|---|---|
| Atomicity | Per file: yes (rename). Across files: see limitations 1 and 2 |
| Consistency | Input is validated (amounts, ids, lengths, commas); total money is preserved in the concurrency test |
| Isolation | Per-file read/write locks, tested with parallel clients |
| Durability | `fsync()` of data and directory before reporting success |

### Known limitations

1. **Balance and passbook are two writes.** The balance file is committed first, then
   the passbook row is appended (both while the customers lock is held). A crash between
   them leaves a balance change without a passbook row. A failed passbook write is
   reported on the server console (`[ALERT] passbook write FAILED`).
2. **Loan approval touches two files.** Order: check the customer can receive money,
   save `approved` in `loans.csv`, then credit `customers.csv`. A crash between the last
   two leaves "approved, not credited" - visible and fixable - but never credits twice.
3. Passwords are stored and sent as plain text (not a PDF requirement).
4. The server uses threads, not child processes, to serve clients.

## 5. Tests

Run the tests against a server started on **fresh data** (`make setup`, or the shipped
sample data), because the tests create and change records:

```bash
make setup
./server 18080 &                         # or in another terminal
python3 tests/functional.py 18080        # 72 checks: every menu item of all four roles
python3 tests/concurrency.py 18080 data  # 100 parallel transfers; total money must not change
```

Also useful: build with `-fsanitize=address,undefined` for the functional test and
`-fsanitize=thread` for the concurrency test. ThreadSanitizer prints two warnings inside
`session_manager.c`; they are false alarms (the table is protected by a System V semaphore,
which ThreadSanitizer cannot see).

## 6. Five-minute demo script

1. Start `./server`, open two clients. Log in as `customer1` in both: the second is refused.
2. Customer: deposit, withdraw, transfer to `alice`, view history.
3. Customer applies for a loan; Manager assigns it to `employee1`; Employee approves it;
   show the customer's balance and passbook (`LOAN_CREDIT`). Approve again: blocked.
4. Manager deactivates a customer; that customer cannot log in. Employee closes an
   empty account; it cannot be re-activated.
5. Admin adds an employee and promotes/demotes one.
6. Run `python3 tests/concurrency.py 8080 data` and show "PASSED ... SUM PRESERVED".
7. Press Ctrl+C on the server: clean shutdown (`ipcs` shows no leftover shared memory).
