"""Concurrency test: 4 clients log in at the same time and make 25 transfers each
(100 transfers in total, 1.00 each, between the sample accounts 1-4).

It PASSES only if ALL of these are true:
  * no worker thread raised an exception (BrokenPipe, timeout, ...)
  * all 100 transfers got a "Transfer successful" reply
  * the ledger gained exactly 100 TRANSFER_OUT and 100 TRANSFER_IN rows
  * the ledger agrees with the balances (each account's change = IN - OUT)
  * the total money in customers.csv is unchanged

Usage (from the project folder, server started on PORT, fresh data via `make setup`):
    python3 tests/concurrency.py <port> <data-dir>        e.g. python3 tests/concurrency.py 8080 data
Customers 1-4 must not be logged in elsewhere."""
import csv, socket, sys, threading, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
DATA = sys.argv[2] if len(sys.argv) > 2 else "data"
PER_USER = 25
USERS = [("customer1", "pass123", 1), ("customer2", "pass123", 2), ("alice", "alice123", 3), ("bob", "bob123", 4)]
EXPECTED = PER_USER * len(USERS)


def rd(s, deadline=60.0, quiet=3.0):
    """Read one complete reply: it ends with a prompt (': ' or '? '), or the server
    closes the connection, or `quiet` seconds of silence (fallback). No fixed short
    timeout, so a slow disk cannot desynchronise the conversation."""
    out = b""
    start = last = time.time()
    s.settimeout(0.2)
    while True:
        try:
            d = s.recv(4096)
            if not d:
                break
            out += d
            last = time.time()
            if out.endswith((b": ", b"? ")):
                time.sleep(0.05)
                s.settimeout(0.05)
                try:
                    more = s.recv(4096)
                    if more:
                        out += more
                        s.settimeout(0.2)
                        continue
                except socket.timeout:
                    pass
                break
        except socket.timeout:
            if out and time.time() - last > quiet:
                break
        if time.time() - start > deadline:
            raise TimeoutError("no complete reply within %.0fs; got %r" % (deadline, out[-100:]))
    return out.decode(errors="replace")


def tx(s, line):
    s.sendall((line + "\n").encode())
    return rd(s)


def balances():
    with open(DATA + "/customers.csv") as f:
        return {int(r["id"]): float(r["balance"]) for r in csv.DictReader(f)}


def ledger():
    with open(DATA + "/transactions.csv") as f:
        return list(csv.DictReader(f))


errors = []            # every problem is appended here (thread-safe enough: list.append)
done = []              # one entry per completed transfer


def worker(user, pw, my_id):
    s = None
    try:
        s = socket.create_connection(("127.0.0.1", PORT))
        rd(s)
        tx(s, "1"); tx(s, user)
        if "Login successful" not in tx(s, pw):
            errors.append("%s: login failed" % user)
            return
        others = [u[2] for u in USERS if u[2] != my_id]
        for k in range(PER_USER):
            target = others[k % 3]
            tx(s, "4"); tx(s, str(target))
            r = tx(s, "1")
            if "Transfer successful" in r:
                done.append((my_id, target))
            else:
                errors.append("%s: transfer %d to %d failed: %r" % (user, k + 1, target, r[-80:]))
        tx(s, "10")
    except Exception as e:                      # BrokenPipeError, TimeoutError, ...
        errors.append("%s: %s: %s" % (user, type(e).__name__, e))
    finally:
        if s:
            s.close()


bal_before = balances()
led_before = ledger()
total_before = sum(bal_before.values())
print("total before:", total_before, bal_before)

t0 = time.time()
threads = [threading.Thread(target=worker, args=u) for u in USERS]
[t.start() for t in threads]
[t.join() for t in threads]
print("%d transfers (%d clients in parallel) took %.2fs" % (len(done), len(USERS), time.time() - t0))

bal_after = balances()
new_rows = ledger()[len(led_before):]
outs = [r for r in new_rows if r["type"] == "TRANSFER_OUT"]
ins = [r for r in new_rows if r["type"] == "TRANSFER_IN"]
total_after = sum(bal_after.values())
print("total after :", total_after, bal_after)
print("ledger rows added: TRANSFER_OUT=%d TRANSFER_IN=%d (expected %d each)" % (len(outs), len(ins), EXPECTED))

problems = list(errors)
if len(done) != EXPECTED:
    problems.append("only %d of %d transfers completed" % (len(done), EXPECTED))
if len(outs) != EXPECTED or len(ins) != EXPECTED:
    problems.append("ledger has %d OUT / %d IN new rows, expected %d each" % (len(outs), len(ins), EXPECTED))
if abs(total_before - total_after) > 0.001:
    problems.append("total money changed: %.2f -> %.2f" % (total_before, total_after))
for cid in bal_before:                          # ledger must agree with the balances
    delta = sum(float(r["amount"]) for r in ins if int(r["customer_id"]) == cid) \
          - sum(float(r["amount"]) for r in outs if int(r["customer_id"]) == cid)
    if abs((bal_after[cid] - bal_before[cid]) - delta) > 0.001:
        problems.append("account %d: balance changed by %.2f but ledger says %.2f"
                        % (cid, bal_after[cid] - bal_before[cid], delta))

if problems:
    print("\nFAILED:")
    for p in problems[:15]:
        print("  -", p)
    if len(problems) > 15:
        print("  ... and %d more" % (len(problems) - 15))
    sys.exit(1)
print("\nPASSED: 100/100 transfers completed, ledger consistent, SUM PRESERVED (%.2f)" % total_after)
