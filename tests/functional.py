import socket, sys, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
results = []


def read_reply(sock, deadline=30.0, quiet=3.0):
    """Read the server's reply WITHOUT guessing how long it takes.

    A reply is complete when the text ends with a prompt (": " or "? "), when the
    server closes the connection, or (fallback, for replies with no prompt) after
    `quiet` seconds of silence. If nothing completes within `deadline` seconds a
    TimeoutError is raised, so a stalled server shows up as a clear failure.
    The old version stopped after 0.4 s of silence, which broke on slow disks
    (e.g. a UTM virtual machine) because the server fsync()s every write."""
    out = b""
    start = last = time.time()
    sock.settimeout(0.2)
    while True:
        try:
            d = sock.recv(4096)
            if not d:
                break                                   # server closed the connection
            out += d
            last = time.time()
            if out.endswith((b": ", b"? ")):            # a prompt: wait for any trailing bytes
                time.sleep(0.05)
                sock.settimeout(0.05)
                try:
                    more = sock.recv(4096)
                    if more:
                        out += more
                        sock.settimeout(0.2)
                        continue
                except socket.timeout:
                    pass
                break
        except socket.timeout:
            if out and time.time() - last > quiet:
                break
        if time.time() - start > deadline:
            raise TimeoutError("no complete reply from server within %.0fs; got %r" % (deadline, out[-120:]))
    return out.decode(errors="replace")


class Sess:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", PORT))
        self.recv_all()

    def recv_all(self):
        return read_reply(self.s)

    def send(self, line):
        self.s.sendall((line + "\n").encode())
        return self.recv_all()

    def seq(self, *lines):
        out = ""
        for l in lines:
            out += self.send(l)
        return out

    def close(self):
        self.s.close()


def check(name, text, expect):
    ok = expect in text
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f"   (expected {expect!r}; got {text[-200:]!r})"))


def login(role, user, pw):
    c = Sess()
    out = c.seq(str(role), user, pw)
    return c, out


# ---------------- Customer ----------------
c, o = login(1, "customer2", "pass123")
check("customer login", o, "Login successful")
check("customer view balance", c.seq("1"), "Balance: 2000.00")
check("customer deposit", c.seq("2", "500"), "New balance: 2500.00")
check("customer withdraw", c.seq("3", "200"), "New balance: 2300.00")
check("customer withdraw too much", c.seq("3", "999999"), "Insufficient")
check("customer negative deposit rejected", c.seq("2", "-5"), "Invalid amount")
check("customer transfer", c.seq("4", "3", "100"), "Transfer successful")
check("customer transfer to missing acct", c.seq("4", "999", "10"), "not found")
o = c.seq("5", "1000", "Personal", "2")
check("customer apply loan", o, "Loan application successful")
import re
LID = re.search(r"Loan ID: (\d+)", o).group(1)
check("customer loan status", c.seq("6"), "pending")
check("customer feedback", c.seq("8", "Great service"), "submitted")
check("customer transactions", c.seq("9"), "TRANSFER_OUT")
check("customer change password", c.seq("7", "pass123", "newpass1"), "Password changed")
check("customer change password back", c.seq("7", "newpass1", "pass123"), "Password changed")
check("customer logout", c.send("10"), "Logged out")
c.close()

# duplicate login
c1, o1 = login(1, "customer1", "pass123")
c2, o2 = login(1, "customer1", "pass123")
check("one session per user (2nd refused)", o2, "already logged in")
c1.send("10"); c1.close(); c2.close()
c3, o3 = login(1, "customer1", "pass123")
check("login works again after logout", o3, "Login successful")
c3.send("10"); c3.close()

c, o = login(1, "customer1", "wrong")
check("wrong password refused", o, "Authentication failed")
c.close()

# ---------------- Manager ----------------
m, o = login(3, "manager1", "pass123")
check("manager login", o, "Login successful")
check("manager deactivate customer 4", m.seq("1", "4", "0"), "deactivated")
d, od = login(1, "bob", "bob123")
check("deactivated customer cannot login", od, "deactivated")
d.close()
check("manager activate customer 4", m.seq("1", "4", "1"), "activated")
check("manager assign the new loan to employee 1", m.seq("2", LID, "1"), "Loan assigned")
check("manager view feedback", m.seq("3"), "Great service")
check("manager mark reviewed", m.send("y"), "reviewed")
check("manager change password", m.seq("4", "pass123", "mgr2"), "Password changed")
check("manager change password back", m.seq("4", "mgr2", "pass123"), "Password changed")
check("manager logout", m.send("5"), "Logged out")
m.close()

# ---------------- Employee ----------------
e, o = login(2, "employee1", "pass123")
check("employee login", o, "Login successful")
check("employee add customer", e.seq("1", "newcust", "np1", "750"), "Customer created")
check("employee modify customer", e.seq("2", "4", "bob2", "-"), "Customer updated")
check("employee modify customer back", e.seq("2", "4", "bob", "-"), "Customer updated")
check("employee view assigned loans", e.seq("4"), "Customer: 2")
check("employee approve the new loan", e.seq("3", LID, "1"), "Loan approved")
check("employee approve again blocked", e.seq("3", LID, "1"), "already been processed")
check("employee passbook", e.seq("5", "2"), "LOAN_CREDIT")
check("employee change password", e.seq("6", "pass123", "emp2"), "Password changed")
check("employee change password back", e.seq("6", "emp2", "pass123"), "Password changed")
check("employee logout", e.send("8"), "Logged out")
e.close()

# ---------------- Admin ----------------
a, o = login(4, "admin1", "pass123")
check("admin login", o, "Login successful")
check("admin add employee", a.seq("1", "emp_new", "ep1"), "Employee created")
check("admin modify customer", a.seq("2", "4", "bob", "bob123"), "Customer updated")
check("admin modify employee", a.seq("3", "1", "employee1", "pass123"), "Employee updated")
check("admin promote employee 2 to manager", a.seq("4", "1", "2"), "promoted")
check("admin demote manager 2 to employee", a.seq("4", "2", "2"), "demoted")
check("admin change password", a.seq("5", "pass123", "adm2"), "Password changed")
check("admin change password back", a.seq("5", "adm2", "pass123"), "Password changed")
check("admin logout", a.send("6"), "Logged out")
a.close()

# persistence of new things
n, o = login(1, "newcust", "np1")
check("new customer can login", o, "Login successful")
check("new customer opening balance", n.send("1"), "750.00")
n.send("10"); n.close()


# ---------------- NEW: close account (employee), loan safety, closed-state rules ----------------
e, o = login(2, "employee1", "pass123")
check("close: employee menu shows Close Customer Account", o, "7. Close Customer Account")
check("close: refused while balance > 0", e.seq("7", "4", "YES"), "Balance must be 0.00")
check("close: wrong confirmation cancels", e.seq("7", "4", "no"), "Cancelled")
check("close: unknown id", e.seq("7", "9999", "YES"), "Account not found")
check("close: invalid id", e.seq("7", "abc"), "Invalid ID")

# empty out 'newcust' (id 5, balance 750) then close it
n, o = login(1, "newcust", "np1")
check("close: customer empties account", n.seq("3", "750"), "New balance: 0.00")
n.send("10"); n.close()

check("close: employee closes zero-balance account", e.seq("7", "5", "YES"), "Account closed")
check("close: closing twice is reported", e.seq("7", "5", "YES"), "already closed")
check("close: passbook keeps history + closing row", e.seq("5", "5"), "ACCOUNT_CLOSED")

n, o = login(1, "newcust", "np1")
check("close: closed customer cannot log in", o, "closed")
n.close()

m, o = login(3, "manager1", "pass123")
check("close: manager cannot re-activate a closed account", m.seq("1", "5", "1"), "closed and cannot be changed")

c, o = login(1, "customer1", "pass123")
check("close: transfer to closed account refused", c.seq("4", "5", "1"), "deactivated or closed")
c.send("10"); c.close()

# loan approval must not credit a deactivated customer
a, o = login(1, "alice", "alice123")
check("loan: alice applies", a.seq("5", "300", "Home", "3"), "Loan application successful")
a.send("10"); a.close()
loan_id = [l.split(",")[0] for l in open("data/loans.csv").read().strip().splitlines()[1:]][-1]
check("loan: manager assigns", m.seq("2", loan_id, "1"), "Loan assigned")
check("loan: manager deactivates alice", m.seq("1", "3", "0"), "deactivated")
check("loan: approval for deactivated customer leaves loan pending", e.seq("3", loan_id, "1"), "loan left pending")
check("loan: still pending in file", open("data/loans.csv").read(), f"{loan_id},3,300.00,Home,3,pending,1")
check("loan: manager re-activates alice", m.seq("1", "3", "1"), "activated")
def bal(uid):
    for l in open("data/customers.csv").read().splitlines()[1:]:
        f = l.split(",")
        if f[0] == str(uid):
            return float(f[3])

before = bal(3)
check("loan: approval now works", e.seq("3", loan_id, "1"), "Loan approved")
check("loan: credited exactly once (balance + 300)", "%.2f" % (bal(3) - before), "300.00")
check("loan: approving again is blocked (no double credit)", e.seq("3", loan_id, "1"), "already been processed")
check("loan: balance unchanged by the blocked second approval", "%.2f" % (bal(3) - before), "300.00")
e.send("8"); e.close(); m.send("5"); m.close()

passed = sum(1 for _, ok in results if ok)
print(f"\n{passed}/{len(results)} checks passed")
sys.exit(0 if passed == len(results) else 1)
