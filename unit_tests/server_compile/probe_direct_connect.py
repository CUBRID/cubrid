#!/usr/bin/env python3
"""probe_direct_connect.py - raw probe of adoption DIRECT_CONNECT (stage B5 PR2).

Connects straight to the server's per-DB adoption UNIX socket (no broker) and:
 1. DIRECT_CONNECT as DB_CLIENT_TYPE_CSQL -> CAS connect reply on the same fd
 2. runs a server-rendered CSQL_REQUEST ("SELECT 1;") on that fd
 3. cancel: second connection sends CANCEL{token} (no HELLO needed)
 4. reject: non-csql client type (BROKER=4) -> HANDOFF_REJECT NOT_AUTHORIZED
 5. reject: wrong dbname -> HANDOFF_REJECT DBNAME_MISMATCH

usage: probe_direct_connect.py <socket_path> <dbname> <dbuser> <dbpasswd>
"""

import socket
import struct
import sys

PROTO_V12 = 12
CAS_PROTO_INDICATOR = 0x40
DB_INFO_SIZE = 628

ADOP_MAGIC = 0x41444F50
OP_DIRECT_CONNECT = 6
OP_CANCEL = 3
OP_HANDOFF_REJECT = 11

REJECT_DBNAME_MISMATCH = 2
REJECT_NOT_AUTHORIZED = 6

CLIENT_TYPE_CSQL = 2
CLIENT_TYPE_BROKER = 4

CAS_FC_CSQL_REQUEST = 45
SUB_EXECUTE = 1
FLAG_AUTO_COMMIT = 0x1
CHUNK_END = 0
CHUNK_OUT = 1


def die(msg):
    print("PROBE_DC FAIL: " + msg)
    sys.exit(1)


def recv_exact(sock, n, what):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            die("connection closed while reading %s (%d/%d bytes)" % (what, len(buf), n))
        buf += chunk
    return buf


def driver_header():
    header = bytearray(10)
    header[0:5] = b"CUBRK"
    header[5] = 3
    header[6] = CAS_PROTO_INDICATOR | PROTO_V12
    return bytes(header)


def db_info(dbname, dbuser, dbpasswd):
    buf = bytearray(DB_INFO_SIZE)
    buf[0:len(dbname)] = dbname.encode()
    buf[32:32 + len(dbuser)] = dbuser.encode()
    buf[64:64 + len(dbpasswd)] = dbpasswd.encode()
    url = b"probe_direct_connect"
    buf[96:96 + len(url)] = url
    return bytes(buf)


def adop_msg(op, body):
    return struct.pack("<III", ADOP_MAGIC, op, len(body)) + body


def direct_connect_body(client_type, dbname, dbuser, dbpasswd):
    return struct.pack("<B3x", client_type) + driver_header() + db_info(dbname, dbuser, dbpasswd)


def open_direct(path, client_type, dbname, dbuser, dbpasswd):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(30)
    sock.connect(path)
    sock.sendall(adop_msg(OP_DIRECT_CONNECT, direct_connect_body(client_type, dbname, dbuser, dbpasswd)))
    return sock


def expect_reject(sock, expected_reason, what):
    head = recv_exact(sock, 12, what + " reject header")
    magic, op, length = struct.unpack("<III", head)
    if magic != ADOP_MAGIC or op != OP_HANDOFF_REJECT or length != 4:
        die("%s: expected HANDOFF_REJECT frame, got magic=0x%x op=%d len=%d" % (what, magic, op, length))
    reason = struct.unpack("<i", recv_exact(sock, 4, what + " reject body"))[0]
    if reason != expected_reason:
        die("%s: expected reject reason %d, got %d" % (what, expected_reason, reason))
    sock.close()


def read_connect_reply(sock):
    raw = recv_exact(sock, 4, "reply length")
    if raw[0:1] == b"P":  # "PODA" — an adoption frame means we were rejected
        rest = recv_exact(sock, 8, "adoption frame head")
        op = struct.unpack("<I", rest[0:4])[0]
        die("connect rejected with adoption frame op=%d" % op)
    length = struct.unpack(">i", raw)[0]
    recv_exact(sock, 4, "cas_info")
    if length < 0:
        die("connect refused, error frame length %d" % length)
    body = recv_exact(sock, length, "connect reply body")
    if length != 36:
        die("connect reply length expected 36, got %d" % length)
    token = struct.unpack(">I", body[0:4])[0]
    if token == 0:
        die("token is zero")
    return token


def arg_int(v):
    return struct.pack(">i", 4) + struct.pack(">i", v)


def arg_str(s):
    b = s.encode() + b"\x00"
    return struct.pack(">i", len(b)) + b


def csql_select_1(sock, tolerate_cancel=False):
    body = struct.pack(">b", CAS_FC_CSQL_REQUEST)
    body += arg_int(SUB_EXECUTE) + arg_int(FLAG_AUTO_COMMIT | 0x2) + arg_int(1) + arg_int(-1) + arg_int(0)
    body += arg_str("") + arg_str("") + arg_str("") + arg_str("SELECT 1;")
    sock.sendall(struct.pack(">i", len(body)) + b"\xff\xff\xff\xff" + body)
    length = struct.unpack(">i", recv_exact(sock, 4, "req reply length"))[0]
    recv_exact(sock, 4, "req reply cas_info")
    reply = recv_exact(sock, length, "req reply body")
    result_code = struct.unpack(">i", reply[0:4])[0]
    status = struct.unpack(">i", reply[4:8])[0]
    if result_code < 0:
        die("csql request failed: code=%d" % result_code)
    if status != 0:
        if tolerate_cancel:
            # the pending interrupt from an idle-session cancel is consumed
            # by this (legitimately cancelled) statement
            return False
        die("csql request failed: status=%d" % status)
    pos, out = 8, b""
    while reply[pos] != CHUNK_END:
        tag = reply[pos]
        clen = struct.unpack(">i", reply[pos + 1:pos + 5])[0]
        chunk = reply[pos + 5:pos + 5 + clen]
        if tag == CHUNK_OUT:
            out += chunk
        pos += 5 + clen
    text = out.decode("utf-8", "replace")
    if "row selected" not in text:
        die("rendered output missing rowcount: %r" % text)
    return True


def main():
    if len(sys.argv) != 5:
        die("usage: probe_direct_connect.py <socket_path> <dbname> <dbuser> <dbpasswd>")
    path, dbname, dbuser, dbpasswd = sys.argv[1:5]

    # 1. direct connect as csql
    sock = open_direct(path, CLIENT_TYPE_CSQL, dbname, dbuser, dbpasswd)
    token = read_connect_reply(sock)
    print("PROBE_DC: direct-connected (0 broker), token=%d" % token)

    # 2. server-rendered execute on the same fd
    csql_select_1(sock)
    print("PROBE_DC: SELECT 1 rendered over direct connection")

    # 3. cancel via a second adoption connection
    qc = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    qc.settimeout(10)
    qc.connect(path)
    qc.sendall(adop_msg(OP_CANCEL, struct.pack("<I", token)))
    qc.close()  # CANCEL has no reply; just must not kill the server
    # an idle-session cancel leaves a pending interrupt; the first statement
    # may consume it as a cancelled statement, the session must then answer
    if not csql_select_1(sock, tolerate_cancel=True):
        csql_select_1(sock)
    print("PROBE_DC: cancel frame accepted, session still healthy")

    # 4. non-csql client type refused
    s2 = open_direct(path, CLIENT_TYPE_BROKER, dbname, dbuser, dbpasswd)
    expect_reject(s2, REJECT_NOT_AUTHORIZED, "broker-type direct connect")
    print("PROBE_DC: non-csql client type refused (NOT_AUTHORIZED)")

    # 5. wrong dbname refused
    s3 = open_direct(path, CLIENT_TYPE_CSQL, "no_such_db_xyz", dbuser, dbpasswd)
    expect_reject(s3, REJECT_DBNAME_MISMATCH, "wrong-db direct connect")
    print("PROBE_DC: wrong dbname refused (DBNAME_MISMATCH)")

    sock.close()
    print("PROBE_DC: SUCCESS")


if __name__ == "__main__":
    main()
