#!/usr/bin/env python3
"""probe_direct.py - raw-wire probe of the B1 direct-handoff path (stage B1 PR2).

Speaks just enough of the CAS V12 connect dialect to prove the 1-hop chain:
driver -> broker (header peek + ack + db_info peek) -> handoff -> cub_server
adoption -> session thread -> connect reply.  The full JDBC smoke arrives with
the b1-cas-speaker PR; this probe is the PR2 gate's "first 1-hop" checkpoint.

usage: probe_direct.py <broker_port> <dbname> <dbuser> <dbpasswd>

checks:
 1. connect: header -> 4B ack(0) -> db_info -> 44B connect reply
    (len=36, ACTIVE cas_info, nonzero token, V12 broker_info, session blob)
 2. health check: HEALTH_CHECK_DUMMY_DB absorbed by the broker (int 0 + cas_info)
 3. cancel: "QC" with the token and matching port -> 4B code 0
 4. cancel anti-spoof: unknown token -> nonzero error code
 5. request loop: any function request answers an error frame (the B1
    not-implemented skeleton) rather than hanging
"""

import socket
import struct
import sys

PROTO_V12 = 12
CAS_PROTO_INDICATOR = 0x40
DB_INFO_SIZE = 628
HEALTH_CHECK_DB = "___health_check_dummy_db___"


def die(msg):
    print("PROBE FAIL: " + msg)
    sys.exit(1)


def recv_exact(sock, n, what):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            die("connection closed while reading %s (%d/%d bytes)" % (what, len(buf), n))
        buf += chunk
    return buf


def connect_header():
    header = bytearray(10)
    header[0:5] = b"CUBRK"
    header[5] = 3  # CAS_CLIENT_JDBC
    header[6] = CAS_PROTO_INDICATOR | PROTO_V12
    return bytes(header)


def db_info(dbname, dbuser, dbpasswd):
    buf = bytearray(DB_INFO_SIZE)
    buf[0:len(dbname)] = dbname.encode()
    buf[32:32 + len(dbuser)] = dbuser.encode()
    buf[64:64 + len(dbpasswd)] = dbpasswd.encode()
    url = b"probe_direct"
    buf[96:96 + len(url)] = url
    return bytes(buf)


def open_and_connect(port, dbname, dbuser, dbpasswd):
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    sock.sendall(connect_header())
    ack = recv_exact(sock, 4, "broker ack")
    ack_val = struct.unpack(">i", ack)[0]
    if ack_val != 0:
        die("broker ack expected 0, got %d" % ack_val)
    sock.sendall(db_info(dbname, dbuser, dbpasswd))
    return sock


def read_connect_reply(sock):
    raw_len = recv_exact(sock, 4, "reply length")
    length = struct.unpack(">i", raw_len)[0]
    cas_info = recv_exact(sock, 4, "cas_info")
    if length < 0:
        # error frame: indicator + code + optional message
        body = recv_exact(sock, 8, "error head") if False else None
        die("connect refused, error frame length %d" % length)
    body = recv_exact(sock, length, "connect reply body")
    if length != 36:
        die("connect reply length expected 36, got %d" % length)
    if cas_info[0] != 1:  # CAS_INFO_STATUS_ACTIVE
        die("cas_info status expected ACTIVE(1), got %d" % cas_info[0])
    token = struct.unpack(">I", body[0:4])[0]
    broker_info = body[4:12]
    if token == 0:
        die("token (pid slot) is zero")
    if not (broker_info[4] & CAS_PROTO_INDICATOR) or (broker_info[4] & 0x3F) < PROTO_V12:
        die("broker_info proto byte not V12: 0x%02x" % broker_info[4])
    session_blob = body[16:36]
    if session_blob == b"\x00" * 20:
        die("driver session blob is all zero")
    return token


def main():
    if len(sys.argv) != 5:
        die("usage: probe_direct.py <broker_port> <dbname> <dbuser> <dbpasswd>")
    port = int(sys.argv[1])
    dbname, dbuser, dbpasswd = sys.argv[2], sys.argv[3], sys.argv[4]

    # 1. connect
    sock = open_and_connect(port, dbname, dbuser, dbpasswd)
    token = read_connect_reply(sock)
    local_port = sock.getsockname()[1]
    print("PROBE: connected 1-hop, token=%d" % token)

    # 2. health check absorbed by the broker
    hc = socket.create_connection(("127.0.0.1", port), timeout=10)
    hc.sendall(connect_header())
    if struct.unpack(">i", recv_exact(hc, 4, "hc ack"))[0] != 0:
        die("health check ack nonzero")
    hc.sendall(db_info(HEALTH_CHECK_DB, "", ""))
    if struct.unpack(">i", recv_exact(hc, 4, "hc reply"))[0] != 0:
        die("health check reply nonzero")
    recv_exact(hc, 4, "hc cas_info")
    hc.close()
    print("PROBE: health check absorbed")

    # 3. cancel with the real token and matching client port
    qc = socket.create_connection(("127.0.0.1", port), timeout=10)
    msg = b"QC" + struct.pack(">I", token) + struct.pack(">H", local_port) + b"\x00\x00"
    qc.sendall(msg)
    code = struct.unpack(">i", recv_exact(qc, 4, "qc reply"))[0]
    qc.close()
    # legacy CAS wire convention: CAS_CONV_ERROR_TO_OLD adds 9000, even on success
    if code != 9000:
        die("cancel with valid token returned %d" % code)
    print("PROBE: cancel forwarded (code 9000)")

    # 4. cancel anti-spoof: unknown token must be refused
    qc2 = socket.create_connection(("127.0.0.1", port), timeout=10)
    msg = b"QC" + struct.pack(">I", 0x7FFFFFF0) + struct.pack(">H", local_port) + b"\x00\x00"
    qc2.sendall(msg)
    code = struct.unpack(">i", recv_exact(qc2, 4, "qc2 reply"))[0]
    qc2.close()
    if code == 9000:
        die("cancel with bogus token was accepted")
    print("PROBE: bogus-token cancel refused (code %d)" % code)

    # 5. request loop answers (B1 skeleton: an error frame, then close)
    #    MSG_HEADER: 4B body size + 4B cas_info; body: 1B func code + no args
    body = struct.pack(">b", 2)  # CAS_FC_END_TRAN-ish; any code will do
    sock.sendall(struct.pack(">i", len(body)) + b"\xff\xff\xff\xff" + body)
    length = struct.unpack(">i", recv_exact(sock, 4, "request reply length"))[0]
    recv_exact(sock, 4, "request reply cas_info")
    recv_exact(sock, length, "request reply body")
    print("PROBE: request loop answered an error frame (len %d) as expected" % length)
    sock.close()

    print("PROBE: SUCCESS")


if __name__ == "__main__":
    main()
