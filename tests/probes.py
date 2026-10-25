import os, shutil, signal, socket, subprocess, sys, threading, time

S = "/tmp/bms_probe_work"
SRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
res = []


def rep(name, ok, detail=""):
    res.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name + (("   -> " + detail) if detail else ""))


def fresh(tag, extra_flags=()):
    d = f"{S}/p_{tag}"
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(SRC, d)
    subprocess.run(["make", "clean"], cwd=d, capture_output=True)
    if extra_flags:
        subprocess.run(["gcc", "-std=gnu11", "-pthread", *extra_flags, "server.c", "signals.c", "auth.c", "records.c",
                        "customer.c", "employee.c", "manager.c", "admin.c", "utils.c", "session_manager.c",
                        "-o", "server"], cwd=d, check=True, capture_output=True)
    else:
        subprocess.run(["make", "server"], cwd=d, check=True, capture_output=True)
    return d


def start(d, port):
    p = subprocess.Popen(["./server", str(port)], cwd=d, stdout=open(d + "/server.log", "w"), stderr=subprocess.STDOUT)
    time.sleep(1.0)
    return p


def stop(p, sig=signal.SIGINT):
    p.send_signal(sig)
    try:
        p.wait(timeout=8)
    except Exception:
        p.kill()


class C:
    def __init__(self, port, to=0.35):
        self.s = socket.create_connection(("127.0.0.1", port))
        self.s.settimeout(to)
        self.rd()

    def rd(self):
        out = b""
        while True:
            try:
                x = self.s.recv(65536)
                if not x:
                    out += b"<EOF>"
                    break
                out += x
            except socket.timeout:
                break
            except OSError:
                out += b"<ERR>"
                break
        return out.decode(errors="replace")

    def tx(self, *lines):
        o = ""
        for l in lines:
            try:
                self.s.sendall((l + "\n").encode())
            except OSError:
                return o + "<SENDFAIL>"
            o += self.rd()
        return o

    def login(self, role, u, p):
        return self.tx(str(role), u, p)


# ---------------------------------------------------------------- P1/P2/P4/P5/P7/P8
d = fresh("a"); port = 19100; p = start(d, port)

c = C(port); o = c.login(1, "customer1", "pass123")
rep("P1 customer menu has no 'close account'", "Close" not in c.tx("") and "close" not in o.lower())
c.tx("10")
o = c.tx("5")
rep("P1 role-menu Exit (5) says Goodbye and closes", "Goodbye" in o)

# P2 abrupt disconnect then relogin
c = C(port); c.login(1, "customer1", "pass123"); c.s.close(); time.sleep(0.8)
c = C(port); o = c.login(1, "customer1", "pass123")
rep("P2 abrupt disconnect frees the session slot", "Login successful" in o)

# P4 input validation (customer1 still logged in on c)
v = {}
v["menu text"] = c.tx("abc")
rep("P4 non-numeric menu choice -> Invalid choice", "Invalid choice" in v["menu text"])
for bad in ["nan", "inf", "1e400", "-0", "0", "abc", "5x", "1000000001", ""]:
    o = c.tx("2", bad)
    rep(f"P4 deposit amount {bad!r} rejected", "Invalid amount" in o, o.strip().splitlines()[-1] if o.strip() else "")
o = c.tx("2", "1000000000"); rep("P4 deposit at MAX_AMOUNT accepted", "Success" in o)
o = c.tx("2", "1000000000"); rep("P4 repeated deposits beyond 1e9 total (no balance cap) -> NOTE", "Success" in o,
                                 "balance can exceed MAX_AMOUNT; harmless for the PDF")
o = c.tx("4", "1", "5"); rep("P4 transfer to self rejected", "Invalid transfer" in o)
o = c.tx("4", "2", "0"); rep("P4 transfer 0 rejected", "Invalid transfer" in o)
o = c.tx("4", "abc"); rep("P4 transfer bad recipient id rejected", "Invalid recipient" in o)
o = c.tx("5", "100", "Car"); rep("P4 loan bad type rejected", "Invalid loan type" in o)
o = c.tx("5", "100", "Home", "0"); rep("P4 loan tenure 0 rejected", "Invalid tenure" in o)
o = c.tx("5", "100", "Home", "31"); rep("P4 loan tenure 31 rejected", "Invalid tenure" in o)
o = c.tx("5", "-5"); rep("P4 loan negative amount rejected", "Invalid amount" in o)
o = c.tx("7", "pass123", "a,b"); rep("P4 password with comma rejected", "Invalid password" in o)
o = c.tx("7", "WRONG", "newone"); rep("P4 change password with wrong old password rejected", "Incorrect" in o)
o = c.tx("8", ""); rep("P4 empty feedback rejected", "cannot be empty" in o)
o = c.tx("8", "x" * 5000); rep("P4 5000-char feedback does not crash server", p.poll() is None and "submitted" in o)
c.tx("10"); c.s.close()

# very long username at login
c = C(port); o = c.login(1, "A" * 5000, "x"); rep("P4 5000-char username does not crash server", p.poll() is None and "failed" in o)
c.s.close()

# P5 deactivated mid-session
c1 = C(port); c1.login(1, "alice", "alice123")
m = C(port); m.login(3, "manager1", "pass123")
m.tx("1", "3", "0")
o = c1.tx("2", "10"); rep("P5 deactivated customer already logged in cannot deposit", "deactivated" in o.lower())
o = c1.tx("3", "10"); rep("P5 ... cannot withdraw", "deactivated" in o.lower())
o = c1.tx("4", "2", "10"); rep("P5 ... cannot transfer", "deactivated" in o.lower())
m.tx("1", "3", "1")
o = c1.tx("2", "10"); rep("P5 after re-activation deposit works again", "Success" in o)

# P7 loan rules
c1.tx("5", "500", "Personal", "1")
loans = open(d + "/data/loans.csv").read()
lid = [l.split(",")[0] for l in loans.strip().splitlines()[1:]][-1]
e = C(port); e.login(2, "employee1", "pass123")
o = e.tx("3", lid, "1"); rep("P7 employee cannot approve a loan not assigned to them", "not assigned" in o)
o = m.tx("2", lid, "999"); rep("P7 manager cannot assign loan to non-existent employee", "Employee not found" in o)
o = m.tx("2", "9999", "1"); rep("P7 manager assign unknown loan id handled", "not found" in o)
m.tx("2", lid, "1")
m.tx("1", "3", "0")   # deactivate alice (the applicant) before approval
o = e.tx("3", lid, "1")
rep("P7 loan for a DEACTIVATED customer is NOT credited (stays pending)", "loan left pending" in o.lower(), o.strip().splitlines()[-1] if o.strip() else "")
o = e.tx("3", "9999", "1"); rep("P7 unknown loan id handled", "not found" in o)
o = e.tx("3", lid, "5"); rep("P7 approve flag other than 0/1 rejected", "enter 1 or 0" in o.lower())
for x in (c1, m, e):
    x.s.close()
stop(p)

# ---------------------------------------------------------------- P6 crash / restart / second instance
d = fresh("b"); port = 19200; p = start(d, port)
c = C(port); c.login(1, "customer1", "pass123")
p2 = subprocess.run(["./server", str(port + 1)], cwd=d, capture_output=True, text=True, timeout=10)
rep("P6 second server instance refused (shared-memory check)", p2.returncode != 0 and "already running" in p2.stderr,
    (p2.stderr.strip().splitlines() or [""])[0])
p.send_signal(signal.SIGKILL); p.wait(); time.sleep(0.5)
leftover = subprocess.run("ipcs -m | grep -c 0x0000270f", shell=True, capture_output=True, text=True).stdout.strip()
rep("P6 after kill -9 the shared memory is left behind (expected)", leftover == "1", f"segments with key 9999: {leftover}")
p = start(d, port)
c = C(port); o = c.login(1, "customer1", "pass123")
rep("P6 restart after kill -9: server starts and user can log in again", "Login successful" in o)
tmp = [f for f in os.listdir(d + "/data") if f.endswith(".tmp")]
rep("P6 no stray .tmp files after kill -9", not tmp, str(tmp))
c.tx("10"); c.s.close(); stop(p)

# ---------------------------------------------------------------- P10 racing logins
d = fresh("c"); port = 19300; p = start(d, port)
wins = []


def racer():
    x = C(port, 1.0); o = x.login(1, "customer1", "pass123")
    if "Login successful" in o:
        wins.append(x)
    else:
        x.s.close()


ts = [threading.Thread(target=racer) for _ in range(30)]
[t.start() for t in ts]; [t.join() for t in ts]
rep("P10 30 simultaneous logins of ONE account: exactly one wins", len(wins) == 1, f"winners={len(wins)}")

# ---------------------------------------------------------------- P11 connection cap
socks = []
refused = 0
for i in range(110):
    try:
        s = socket.create_connection(("127.0.0.1", port)); s.settimeout(0.3); socks.append(s)
    except OSError:
        refused += 1
time.sleep(0.5)
busy = 0
for s in socks:
    try:
        if b"busy" in s.recv(4096):
            busy += 1
    except Exception:
        pass
rep("P11 clients beyond MAX_CLIENTS (100) get 'Server is busy'", busy >= 5, f"busy={busy}, refused={refused}")
for s in socks:
    s.close()
for w in wins:
    w.s.close()
stop(p)

# ---------------------------------------------------------------- P3 idle timeout (compiled with -D, source unchanged)
d = fresh("d", ("-DCLIENT_IDLE_TIMEOUT_SEC=2",)); port = 19400; p = start(d, port)
c = C(port, 4.5); c.login(1, "customer1", "pass123")
o = c.rd()
rep("P3 idle client is disconnected with a timeout message", "inactivity" in o.lower() or "EOF" in o, o.strip()[-80:])
time.sleep(0.5)
c = C(port); o = c.login(1, "customer1", "pass123")
rep("P3 session slot freed after idle timeout", "Login successful" in o)
c.s.close(); stop(p)

# ---------------------------------------------------------------- password storage evidence
rep("SEC passwords are stored in PLAIN TEXT in data/customers.csv",
    "pass123" in open(SRC + "/data/customers.csv").read(), "(security weakness, not a PDF item)")

print("\n%d/%d probes behaved as stated" % (sum(1 for _, k in res if k), len(res)))
