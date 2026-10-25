# Linux Run Guide - Banking Management System (v3)

A step-by-step guide for running, testing and demonstrating this project on
**Ubuntu / Linux**. Every command and every output shown here was taken from the
real project files (`Makefile`, `README.md`, the source code and the `tests/` scripts).

> You will use **three terminal windows**: Terminal 1 = server, Terminal 2 = client,
> Terminal 3 = a second client (for the demo) or the tests.

---

## 1. Required software (Ubuntu)

| Software | Why you need it |
|---|---|
| `gcc` and `make` | compile the C code (`build-essential` installs both) |
| `unzip` | extract the project ZIP |
| `python3` | run the test scripts in `tests/` (usually already installed on Ubuntu) |

Install everything with:

```bash
sudo apt update
sudo apt install -y build-essential unzip python3
```

Check that it worked:

```bash
gcc --version      # prints something like: gcc (Ubuntu ...) ...
make --version     # prints something like: GNU Make 4.x
python3 --version  # prints something like: Python 3.x.x
```

(`ipcs` and `ipcrm`, used in this guide, are part of Ubuntu already.)

---

## 2. Extract the ZIP and enter the project folder

Put `Banking-Management-System-v3.zip` in your home folder (or any folder), then:

```bash
cd ~                                   # or the folder that contains the ZIP
unzip Banking-Management-System-v3.zip
cd Banking-Management-System-v3
ls
```

Expected: these 32 names (your terminal prints them in columns, in alphabetical order):

```
admin.c  admin.h  auth.c  auth.h  class_diagram.dot  class_diagram.pdf  class_diagram.png
client.c  client.h  CMakeLists.txt  common.h  customer.c  customer.h  data  employee.c
employee.h  LINUX_RUN_GUIDE.md  Makefile  manager.c  manager.h  README.md  records.c
records.h  server.c  server.h  session_manager.c  session_manager.h  signals.c
signals.h  tests  utils.c  utils.h
```

**Every command in this guide must be run from
this folder.** If you see `make: *** No targets specified and no makefile found`,
you are in the wrong folder: run `cd ~/Banking-Management-System-v3` (adjust the path to
where you extracted it).

---

## 3. Compile the project

```bash
make
```

`make` reads the `Makefile` and builds two programs, `server` and `client`.
Expected output (no warnings):

```
gcc -g -std=gnu11 -Wall -Wextra -pthread server.c signals.c auth.c records.c customer.c employee.c manager.c admin.c utils.c session_manager.c -o server -pthread
gcc -g -std=gnu11 -Wall -Wextra -pthread client.c -o client
✓ Build complete!
```

To delete the compiled programs and build again from scratch: `make clean` then `make`.

---

## 4. Terminal 1: start the server

```bash
cd ~/Banking-Management-System-v3
./server
```

Expected output (the PID number will be different on your computer):

```
✓ Session manager initialized (shm key 9999, sem key 10000)
Server listening on port 8080 (PID 305)
  Ctrl+C / kill 305       -> graceful shutdown
  kill -USR1 305          -> print server status
```

The server listens on port **8080** by default. To use another port: `./server 9000`.
Leave this terminal open. When clients connect and log in, you will see lines such as
`[+] Client 127.0.0.1:59212 connected (fd=4)` and
`[auth] Customer 'alice' (id=3) logged in from 127.0.0.1:59212`.

---

## 5. Terminal 2: start the client

Open a **new** terminal window:

```bash
cd ~/Banking-Management-System-v3
./client
```

(Default: server `127.0.0.1`, port `8080`. For another port: `./client 127.0.0.1 9000`.
For a server on another computer: `./client <server-ip> 8080`.)

Expected output:

```
Connected to server 127.0.0.1:8080  (Ctrl+C to quit)


=== Banking System ===
1. Customer
2. Employee
3. Manager
4. Admin
5. Exit
Select role: 
```

If you see `Connection failed: Connection refused`, the server is not running: start
it in Terminal 1 first.

---

## 6. Sample login credentials

Type the role number, then the username, then the password.

| Role | Role number | Username | Password |
|---|---|---|---|
| Customer | 1 | `customer1` | `pass123` |
| Customer | 1 | `customer2` | `pass123` |
| Customer | 1 | `alice` | `alice123` |
| Customer | 1 | `bob` | `bob123` |
| Employee | 2 | `employee1` | `pass123` |
| Manager | 3 | `manager1` | `pass123` |
| Administrator | 4 | `admin1` | `pass123` |

Customer account numbers (IDs): `customer1` = 1, `customer2` = 2, `alice` = 3, `bob` = 4.
Starting balances after `make setup`: 1000.00, 2000.00, 5000.00, 3000.00.
(The ZIP's own sample data has `customer1` = 1100.00 and one old approved loan.
Run `make setup`, section 9, to get the clean numbers used in this guide.)

Example login:

```
Select role: 1
Username: customer1
Password: pass123

✓ Login successful!
```

Login problems and the exact messages you will see:

| Situation | Message |
|---|---|
| Wrong username or password | `✗ Authentication failed!` |
| Same account already logged in somewhere else | `✗ This account is already logged in from another session.` |
| Customer deactivated by a manager | `✗ Account is deactivated. Please contact the bank.` |
| Customer closed by an employee | `✗ This account has been closed. Please contact the bank.` |

---

## 7. How to use each menu

After every action the menu appears again. Type the number and press Enter.
Money amounts must be positive numbers (for example `100` or `250.50`).

### 7.1 Customer menu (role 1)

```
=== Customer Menu ===
1. View Account Balance
2. Deposit Money
3. Withdraw Money
4. Transfer Funds
5. Apply for a Loan
6. Check Loan Status
7. Change Password
8. Add Feedback
9. View Transaction History
10. Logout
```

| Choice | What you type | Result |
|---|---|---|
| 1 | nothing more | `Balance: 1000.00` |
| 2 | `Enter amount:` -> `100` | `Success! New balance: 1100.00` |
| 3 | `Enter amount:` -> `50` | `Success! New balance: ...` (or `Insufficient balance`) |
| 4 | `Enter recipient ID:` -> `3`, `Enter amount:` -> `200` | `Transfer successful! Balance: ...` |
| 5 | `Enter loan amount:` -> `5000`, `Enter loan type (Personal/Home/Education):` -> `Personal`, `Enter tenure (years, 1-30):` -> `2` | `Loan application successful! Loan ID: 1` |
| 6 | nothing more | a table of your loans with status `pending`, `approved` or `rejected` |
| 7 | `Enter current password:` then `Enter new password:` | `Password changed successfully` |
| 8 | `Enter feedback message:` -> your text | `Feedback submitted successfully` |
| 9 | nothing more | list such as `DEPOSIT: 100.00 | Balance: 1100.00 | 2026-10-23 05:09:16` |
| 10 | nothing more | `Logged out.` and you return to the role menu |

### 7.2 Employee menu (role 2)

```
=== Employee Menu ===
1. Add New Customer
2. Modify Customer Details
3. Process Loan Applications
4. View Assigned Loan Applications
5. View Customer Transactions (Passbook)
6. Change Password
7. Close Customer Account
8. Logout
```

| Choice | What you type | Result |
|---|---|---|
| 1 | `Enter username:`, `Enter password:`, `Initial balance:` | `Customer created! ID: 5` |
| 2 | `Enter Customer ID:`, then `New username (- to skip):`, `New password (- to skip):` (type `-` to keep the old value) | `Customer updated` |
| 3 | `Enter loan ID:`, then `Approve (1) or Reject (0):` | `Loan approved! Amount 5000.00 credited to customer 1's account` or `Loan rejected` |
| 4 | nothing more | loans **assigned to you** by a manager |
| 5 | `Enter customer ID:` | the customer's passbook (all transactions) |
| 6 | current password, new password | `Password changed successfully` |
| 7 | `Enter customer ID to close:`, then type `YES` | `Account closed` |
| 8 | nothing more | `Logged out.` |

Rules to remember: an employee can only process a loan **assigned to them**
(otherwise: `This loan is not assigned to you`); a loan can be processed only once;
an account can be closed only if its balance is `0.00`
(otherwise: `Balance must be 0.00 before closing ...`); a closed account can never be
re-activated. Loans are not credited to deactivated or closed customers.

### 7.3 Manager menu (role 3)

```
=== Manager Menu ===
1. Activate/Deactivate Customer Accounts
2. Assign Loan Application Processes to Employees
3. Review Customer Feedback
4. Change Password
5. Logout
```

| Choice | What you type | Result |
|---|---|---|
| 1 | `Enter customer ID:`, then `Activate (1) or Deactivate (0):` | `Account activated` / `Account deactivated` |
| 2 | `Enter loan ID:`, then `Enter employee ID:` | `Loan assigned` |
| 3 | nothing, then `Mark all feedback as reviewed? (y/n):` | list `ID: 1 | Customer: 1 | Message: ... | Reviewed: No`, then `All feedback marked as reviewed` |
| 4 | current password, new password | `Password changed successfully` |
| 5 | nothing more | `Logged out.` |

### 7.4 Administrator menu (role 4)

```
=== Admin Menu ===
1. Add New Bank Employee
2. Modify Customer Details
3. Modify Employee Details
4. Manage User Roles
5. Change Password
6. Logout
```

| Choice | What you type | Result |
|---|---|---|
| 1 | `Enter username:`, `Enter password:` | `Employee created! ID: 2` |
| 2 | `Enter Customer ID:`, new username / password (`-` = keep) | `Customer updated` |
| 3 | `Enter Employee ID:`, new username / password (`-` = keep) | `Employee updated` |
| 4 | `1` = promote employee to manager or `2` = demote manager to employee, then the ID | `Employee promoted to Manager successfully (new Manager ID: 2)` |
| 5 | current password, new password | `Password changed successfully` |
| 6 | nothing more | `Logged out.` |

Usernames and passwords must be 1-49 characters with **no commas**.

---

## 8. Run the tests

Run the tests in **Terminal 3** while the server is running in Terminal 1, and always
from the project folder (the scripts read the `data/` folder with a relative path).

**Important rules**
1. Start from clean data: stop the server, run `make setup`, start the server again
   (section 9). The tests create and change records, so a second run on the same data
   fails some checks (you will see fewer than `72/72 checks passed`).
2. **Give the port explicitly.** The server uses 8080, but the scripts default to
   18080 if you forget the port.
3. Nobody may be logged in as `customer1`, `customer2`, `alice` or `bob` in another client.

### 8.1 Functional test (every menu item of all four roles)

```bash
cd ~/Banking-Management-System-v3
python3 tests/functional.py 8080
```

It takes about **20-40 seconds** (the script waits until the server has really answered, so a slow disk only makes it slower, never wrong). It exits with an error code if any check fails. Expected
output (each line is `PASS` or `FAIL`):

```
PASS customer login
PASS customer view balance
PASS customer deposit
...
PASS new customer opening balance
...
72/72 checks passed
```

If a line says `FAIL`, it shows what was expected and what the server sent.

### 8.2 Concurrency test (100 transfers at the same time)

```bash
python3 tests/concurrency.py 8080 data
```

The arguments are `<port> <data-folder>`. It starts 4 clients (`customer1`, `customer2`,
`alice`, `bob`) in parallel, each making 25 transfers of 1.00, then checks that the
**total money did not change**. It takes about **10-60 seconds** depending on disk
speed. It passes **only if** (1) no client thread had an error, (2) all 100 transfers
were answered "Transfer successful", (3) the ledger gained exactly 100 `TRANSFER_OUT` and
100 `TRANSFER_IN` rows, (4) every account's balance change matches the ledger, and
(5) the total money is unchanged. Expected output (balances after `make setup`):

```
total before: 11000.0 {1: 1000.0, 2: 2000.0, 3: 5000.0, 4: 3000.0}
100 transfers (4 clients in parallel) took 8.12s
total after : 11000.0 {1: 1002.0, 2: 2000.0, 3: 4999.0, 4: 2999.0}
ledger rows added: TRANSFER_OUT=100 TRANSFER_IN=100 (expected 100 each)

PASSED: 100/100 transfers completed, ledger consistent, SUM PRESERVED (11000.00)
```

If something is wrong it prints `FAILED:` with the reasons and exits with code 1.
Run it right after `make setup` and a fresh server start (customers 1-4 must not be
logged in anywhere else).

(`tests/probes.py` is an extra script with additional checks; it starts its own servers
on ports 19100-19400 and is optional (about 1 minute; expected result: `46/46 probes behaved as stated`).)

---

## 9. Reset the sample data safely (`make setup`)

> **WARNING - DATA LOSS.** `make setup` **overwrites all 7 data files** in `data/`
> (customers, employees, managers, admins, transactions, loans, feedback) with the
> original sample data. Every account, balance, loan and transaction you created is
> lost. There is no undo.

Safe procedure:

```bash
# 1. Terminal 1: stop the server with Ctrl+C (section 10)
# 2. (Optional) keep a backup of your current data
cp -r data data_backup
# 3. Reset
make setup
# 4. Start the server again
./server
```

Expected output of `make setup`:

```
✓ Data setup complete
```

After `make setup` the customers are: `customer1` 1000.00, `customer2` 2000.00,
`alice` 5000.00, `bob` 3000.00; one employee, one manager, one admin, and **no** loans,
transactions or feedback. (To restore your backup: `rm -r data && mv data_backup data`.)

---

## 10. Log out, exit the client, stop the server

**Log out** (inside the client): choose the *Logout* number of your menu
(Customer `10`, Employee `8`, Manager `5`, Admin `6`). You return to the role menu.

**Exit the client**: at the role menu type `5`:

```
Select role: 5
Goodbye!

[client] Server closed the connection.
Disconnected
```

You can also press **Ctrl+C** in the client:
`[client] Caught Interrupt. Closing connection gracefully...` then `Disconnected`.
The server logs the user out automatically.

**Stop the server**: click the Terminal 1 window and press **Ctrl+C** once. Expected:

```
[signal] Caught Interrupt (signal 2). Starting graceful shutdown...
[signal] Press Ctrl+C again to force an immediate exit.
[shutdown] Notified 0 connected client(s). Waiting up to 5 s for threads...
[shutdown] All client threads finished.
✓ Shared memory segment and semaphore removed
[shutdown] Server stopped cleanly. Bye!
```

(Pressing Ctrl+C a second time forces an immediate exit; avoid it.)

Optional check while the server runs: `kill -USR1 <PID>` prints
`[status] PID 305 | uptime 0h 00m 01s | connected clients: 0/100 | logged-in sessions: 0/100`.

---

## 11. Common problems and solutions

| Problem / message | Cause and solution |
|---|---|
| `gcc: command not found` or `make: command not found` | Install the tools: `sudo apt install -y build-essential` |
| `make: *** No targets specified and no makefile found.` | You are in the wrong folder. `cd` into `Banking-Management-System-v3` (the folder that contains `Makefile`). |
| `unzip: command not found` | `sudo apt install -y unzip` |
| `Connection failed: Connection refused` and `Is the server running on 127.0.0.1:8080 ?` | Start `./server` in Terminal 1 first, then the client. If the server uses another port, give it to the client: `./client 127.0.0.1 9000`. |
| `bind: Address already in use` and `Port 8080 is already in use...` | Another server (or program) uses port 8080. Stop it with Ctrl+C in its terminal, or start this one on another port: `./server 9000` and `./client 127.0.0.1 9000`. To find the program: `sudo apt install -y lsof` then `lsof -i :8080`. |
| `Another server instance is already running (shared memory key 9999 is attached by 1 process(es)).` | A server is already running (maybe on another port). Stop it first. |
| Server was killed with `kill -9` or the terminal was closed and leftovers remain | Run `make reset-ipc` (prints `✓ IPC objects removed (if any existed)`). Check with `ipcs -m -s`. Starting the server again also works: it re-uses the old memory. |
| `✗ This account is already logged in from another session.` | The same user is logged in in another terminal. Log out there. If a terminal was closed, wait a moment: the server frees the account when the connection closes. |
| `*** Session timed out due to inactivity. Disconnecting. ***` | The server disconnects a client that sends nothing for **5 minutes**. Just start the client again. |
| `Server is busy, please try again later.` | 100 clients are already connected (the server's limit). |
| Test script prints `FAIL ...` or fewer than 72 checks pass | The data was not clean, or someone is logged in. Stop the server, `make setup`, start the server, and run the test again with the port: `python3 tests/functional.py 8080`. |
| Test script hangs or prints connection errors | Wrong port (the scripts default to 18080) or the server is not running. Use `python3 tests/functional.py 8080`. |
| `Invalid choice` / `Invalid amount` messages | You typed something that is not a menu number or a positive number. Try again. |
| `Insufficient balance` | The withdrawal or transfer is larger than the balance. |
| `This loan is not assigned to you` | A manager must first assign the loan (Manager menu 2) to **this** employee's ID. |

---

## 12. The important source files (simple explanation)

| File | What it does |
|---|---|
| `server.c` | The main program of the server. Opens a **socket**, accepts clients, starts **one thread per client**, shows the role menu, performs login and shuts down cleanly. |
| `client.c` | The text client. Connects to the server, shows what the server sends and sends what you type. Handles Ctrl+C. |
| `auth.c` | Login check (`authenticate`), `change_password` and `modify_account_details` shared by the roles. |
| `session_manager.c` | Keeps the list of logged-in users in **shared memory**, protected by a **semaphore**. This enforces *one session per user*. |
| `customer.c` | All Customer menu actions: balance, deposit, withdraw, transfer, loan application, feedback, history. |
| `employee.c` | Employee actions: add customer, modify customer, approve/reject loans, passbook, close account. |
| `manager.c` | Manager actions: activate/deactivate accounts, assign loans, review feedback. |
| `admin.c` | Admin actions: add employee, promote/demote (manage roles). |
| `records.c` | The only place that knows the layout of each CSV file (reads and writes one line of customers, loans, feedback). |
| `utils.c` | Helpers: one **lock per CSV file**, reading and writing sockets, writing files with system calls (`write`, `fsync`, `rename`), the file lock (`fcntl`), logging. |
| `signals.c` | A dedicated thread waits for Ctrl+C and other signals with `sigwait()`. |
| `common.h` | Shared constants: port 8080, limits (100 clients), role numbers, account states. |
| `data/*.csv` | The "database": plain text files, one per table. |
| `tests/functional.py`, `tests/concurrency.py` | The test scripts of section 8. |
| `class_diagram.pdf` | The class diagram that matches this code. |

Simple picture of how it works:

```
client (you type)  --TCP socket-->  server.c  -> new thread for you -> login (auth.c, session_manager.c)
                                                                   -> your menu (customer/employee/manager/admin.c)
                                                                   -> data/*.csv  (locks in utils.c protect the files)
```

---

## 13. Short demonstration sequence for a college presentation

Prepare: stop the server, run `make setup` (clean data), then start the server.
Use Terminal 1 = server, Terminal 2 = Client A, Terminal 3 = Client B.
All numbers below come from clean sample data.

1. **Start** (Terminal 1): `./server`. Show the lines `Server listening on port 8080`.
2. **Login + one session per user.** Terminal 2: `./client`, role `1`, `customer1` / `pass123`.
   Terminal 3: `./client`, role `1`, `customer1` / `pass123` again ->
   `✗ This account is already logged in from another session.` Then in Terminal 3 log in
   as `alice` (`alice123`) to show two users at once.
3. **Customer operations** (Terminal 2): `1` balance `1000.00`; `2` deposit `500` ->
   `New balance: 1500.00`; `4` transfer to recipient `3` amount `200` ->
   `Transfer successful! Balance: 1300.00`. In Terminal 3 (alice) choose `1`: `Balance: 5200.00`.
4. **Loan flow.**
   - Terminal 2: `5` apply: `5000`, `Personal`, `2` -> `Loan ID: 1`.
   - Terminal 3: log `alice` out (`10`), then log in as the manager: role `3`, `manager1` / `pass123`,
     `2` assign: loan `1` to employee `1` -> `Loan assigned`. Log the manager out (`5`).
   - Terminal 3: role `2`, `employee1` / `pass123`, `3` process: loan `1`, approve `1` ->
     `Loan approved! Amount 5000.00 credited to customer 1's account`.
   - Terminal 2: `1` -> `Balance: 6300.00`; `9` history shows `DEPOSIT`, `TRANSFER_OUT`, `LOAN_CREDIT`.
5. **Manager: deactivate.** `1`, customer `4`, `0` -> `Account deactivated`. Try to log in as
   `bob` / `bob123` -> `✗ Account is deactivated. Please contact the bank.` Then re-activate: `1`, `4`, `1`.
6. **Employee: add and close an account.** Employee menu `1`: username `demo`, password `demo1`,
   balance `0` -> `Customer created! ID: 5`. Then `7`: customer `5`, `YES` -> `Account closed`.
   Logging in as `demo` -> `✗ This account has been closed. Please contact the bank.`
7. **Admin: roles.** Role `4`, `admin1` / `pass123`; `1` add employee `emp2` / `pw2` ->
   `Employee created! ID: 2`; `4` then `1` then ID `2` ->
   `Employee promoted to Manager successfully (new Manager ID: 2)`.
8. **Concurrency + data safety** (a fresh terminal, from the project folder, server running, nobody logged in as
   `customer1`/`customer2`/`alice`/`bob`): `python3 tests/concurrency.py 8080 data` (about 10-60 s) ->
   `PASSED ... SUM PRESERVED`. (Tip: run this *before* the presentation and show the saved output if you are short of time.)
9. **Clean shutdown.** Press Ctrl+C in Terminal 1 -> `Server stopped cleanly. Bye!`; then run
   `ipcs -m -s` to show that the shared memory and semaphore are gone.

### Things to explain in the presentation (very short)

* **Sockets:** the server and the clients talk over TCP (`socket`, `bind`, `listen`, `accept`, `connect`).
* **Threads:** the server makes one thread per client, so many users work at the same time.
* **Locks:** every CSV file has a read/write lock so two threads never change a file at the same moment;
  `fcntl` file locks are also used when a file is rewritten.
* **Semaphore + shared memory:** the list of logged-in users, which gives *one session per user*.
* **Atomic updates:** a file is rewritten as a temp file, `fsync`ed, then `rename`d over the old one.
  A transfer changes both accounts in one file, so money is never lost halfway.
* **Signals:** Ctrl+C is received by a special thread that shuts the server down cleanly.
* **System calls:** files and the console use `open`, `read`, `write`, `fsync`, `rename`, `unlink`
  (no `fopen`, no `printf`), and there is no `fork` (threads are used instead).

---

## 14. Known limitations (say these honestly)

1. **Balance and passbook are two writes.** A crash exactly between them can leave a
   balance change without its passbook line. (A failed passbook write prints
   `[ALERT] passbook write FAILED` on the server.)
2. **Loan approval changes two files** (`loans.csv`, then `customers.csv`). A crash between them can
   leave a loan "approved but not credited" (it can never be credited twice).
3. **Passwords are stored and sent as plain text** (no hashing, no encryption).
4. The server uses **threads, not `fork()`**, to serve clients.
5. The `fcntl` lock protects against other *processes*; between the server's own threads the
   per-file `pthread` locks do the work.
6. Employees and admins can change only the username and password of an account (the data model has no other fields).
