#!/usr/bin/env python3
"""Live histogram regression: permissions, isolation, reset and actual wire bytes.

Run against an already running isolated broker/database:
  python3 probe_histogram.py BROKER_PORT DBNAME
The caller owns server/port setup and teardown. This probe starts no daemons.
"""

import contextlib
import re
import socket
import struct
import sys
import time

import probe_csql as wire


FLAGS = wire.FLAG_AUTO_COMMIT | wire.FLAG_TRIGGER_ACTION


class Connection:
    def __init__(self, port, database, kind=3, user="dba"):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        header = bytearray(wire.connect_header())
        header[5] = kind
        self.sock.sendall(header)
        assert wire.recv_exact(self.sock, 4, "ack") == b"\0" * 4
        self.sock.sendall(wire.db_info(database, user, ""))
        size = struct.unpack(">i", wire.recv_exact(self.sock, 4, "length"))[0]
        wire.recv_exact(self.sock, 4, "cas info")
        body = wire.recv_exact(self.sock, size, "connect")
        assert size == 36, ("connection rejected", body)
        # Reply prefix 16 bytes, then server key 8 bytes and network-order ID.
        self.session = struct.unpack(">I", body[24:28])[0]
        assert self.session != 0
        self.sent = self.received = 0

    def sendall(self, data):
        self.sock.sendall(data)
        self.sent += len(data)

    def recv(self, size):
        data = self.sock.recv(size)
        self.received += len(data)
        return data

    def close(self):
        self.sock.close()

    def execute(self, sql):
        status, chunks = wire.csql_execute(self, sql, FLAGS)
        assert status == 0, (sql, status, chunks)
        return wire.text_of(chunks, wire.CHUNK_OUT)

    def command(self, command, success=True):
        status, chunks = wire.csql_session_cmd(self, command, FLAGS)
        assert (status == 0) == success, (command, status, chunks)
        return wire.text_of(chunks, wire.CHUNK_OUT)

    def version(self):
        # A genuine ordinary JDBC/CCI function, independent of the additive
        # csql request. Its byte argument is autocommit=false.
        body = bytes([15]) + struct.pack(">i", 1) + b"\0"
        reply = wire.send_request(self, body)
        assert struct.unpack(">i", reply[:4])[0] == 0, reply


def rows(output):
    assert "Histogram of client requests:" in output, output
    return {
        match[0]: (int(match[1]), int(match[2]), int(match[3]), float(match[4]))
        for match in re.findall(r"^(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9.]+)$", output, re.M)
    }


def fetches(output):
    match = re.search(r"^Num_data_page_fetches\s*=\s*(\d+)", output, re.M)
    assert match, output
    return int(match[1])


def target_dump(admin, session, expected):
    # A reply can reach the client just before the server publishes its completed
    # request snapshot. Wait on that explicit completion condition, not a sleep.
    deadline = time.monotonic() + 5
    while True:
        output = admin.command(";.dump_hist session %d" % session)
        result = rows(output)
        if result.get("get_db_version", (0,))[0] == expected:
            return result
        assert time.monotonic() < deadline, (expected, output)
        time.sleep(0.01)


def main():
    port, database = int(sys.argv[1]), sys.argv[2]
    with contextlib.ExitStack() as stack:
        def connect(kind=3, user="dba"):
            conn = Connection(port, database, kind, user)
            stack.callback(conn.close)
            return conn

        admin, own, other = connect(), connect(), connect()
        admin.execute("CREATE TABLE hist_probe (n INT); INSERT INTO hist_probe SELECT ROWNUM FROM db_class;")
        admin.execute("CREATE USER hist_probe_user;")
        try:
            assert "OFF" in own.command(";.hist")
            own.command(";.hist on")
            assert not rows(own.command(";.dump_hist"))
            sent, received = own.sent, own.received
            own.execute("SELECT SUM(n) FROM hist_probe;")
            sent, received = own.sent - sent, own.received - received
            before = own.command(";.dump_hist")
            first = rows(before)
            assert first["csql_execute"][:3] == (1, sent, received), first
            assert first["csql_execute"][3] > 0, first
            assert fetches(before) > 0, before
            for _ in range(5):
                other.execute("SELECT SUM(n) FROM hist_probe;")
            after = own.command(";.dump_hist")
            assert rows(after) == first, (first, rows(after))
            assert fetches(after) == fetches(before), (before, after)
            cleared = own.command(";.x_hist")
            assert rows(cleared) == first, cleared
            after_clear = own.command(";.dump_hist")
            assert not rows(after_clear) and fetches(after_clear) == 0, after_clear
            own.execute("SELECT SUM(n) FROM hist_probe;")
            own.command(";.clear_hist")
            assert not rows(own.command(";.dump_hist"))
            own.command(";get isolation_level")
            assert rows(own.command(";.dump_hist"))["csql_session_command"][0] == 1
            own.command(";.hist off")
            assert "currently OFF" in own.command(";.dump_hist")
            print("HISTOGRAM: current-session stats, wire bytes, dump/clear and isolation PASS")

            for kind, count in ((1, 3), (3, 5)):
                target = connect(kind)
                admin.command(";.hist on session %d" % target.session)
                sent, received = target.sent, target.received
                for _ in range(count):
                    target.version()
                sent, received = target.sent - sent, target.received - received
                measured = target_dump(admin, target.session, count)
                assert measured["get_db_version"][:3] == (count, sent, received), measured
                assert measured["get_db_version"][3] > 0, measured
                assert len(measured) == 1, measured
                admin.command(";.clear_hist session %d" % target.session)
                assert not target_dump(admin, target.session, 0)
                admin.command(";.hist off session %d" % target.session)
                target.version()
                admin.command(";.hist on session %d" % target.session)
                assert not target_dump(admin, target.session, 0)
                target.version()
                target_dump(admin, target.session, 1)
                admin.command(";.hist off session %d" % target.session)
            print("HISTOGRAM: CCI/JDBC request counts/bytes/time and targeted reset/on/off PASS")

            unprivileged = connect(user="hist_probe_user")
            assert "CS Name" in admin.command(";info csstat")
            _, denied_chunks = wire.csql_session_cmd(unprivileged, ";info csstat", FLAGS)
            denied = "".join(wire.text_of(denied_chunks, tag) for tag in (wire.CHUNK_OUT, wire.CHUNK_ERR))
            assert "CS Name" not in denied and "DBA" in denied, denied
            for command in (";.hist on", ";.hist on session %d" % own.session,
                            ";.dump_hist session %d" % own.session,
                            ";.clear_hist session %d" % own.session):
                assert "allowed only for DBA" in unprivileged.command(command), command
            assert "currently OFF" in unprivileged.command(";.x_hist")
            for session in ("-1", "4294967296", "abc", "1 trailing"):
                admin.command(";.hist on session " + session, success=False)
            admin.command(";.hist on session 4294967295", success=False)
            fresh = connect()
            assert "OFF" in fresh.command(";.hist")
            fresh.command(";.hist on")
            fresh.execute("SELECT 1;")
            fresh.close()  # EOF while collection is enabled must release it.
            replacement = connect()
            assert "OFF" in replacement.command(";.hist")
            print("HISTOGRAM: DBA enforcement, malformed/missing targets and new-session reset PASS")
        finally:
            admin.execute("DROP TABLE hist_probe;")
            # Disconnect this test's ordinary user before dropping it.
            if "unprivileged" in locals():
                unprivileged.close()
            admin.execute("DROP USER hist_probe_user;")
    print("HISTOGRAM: SUCCESS")


if __name__ == "__main__":
    main()
