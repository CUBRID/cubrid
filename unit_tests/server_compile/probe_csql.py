#!/usr/bin/env python3
"""probe_csql.py - raw-wire probe of CAS_FC_CSQL_REQUEST (stage B5 PR1).

Connects 1-hop through a DIRECT_HANDOFF broker like probe_direct.py, then
exercises the server-rendered csql request:
 1. EXECUTE "SELECT 1;"        -> rendered result table + "1 row selected"
 2. EXECUTE with a syntax error -> ERR-tagged chunk, nonzero failure count
 3. SESSION_CMD ";schema db_class" -> rendered class description
 4. SESSION_CMD ";shell" (client-only) -> refused (DO_CMD_FAILURE), no output
    of a shell run — the SERVER_MODE allowlist gate

usage: probe_csql.py <broker_port> <dbname> <dbuser> <dbpasswd>
"""

import socket
import struct
import sys

PROTO_V12 = 12
CAS_PROTO_INDICATOR = 0x40
DB_INFO_SIZE = 628

CAS_FC_CSQL_REQUEST = 45
SUB_EXECUTE = 1
SUB_SESSION_CMD = 2

FLAG_AUTO_COMMIT = 0x1
FLAG_CONTINUE_ON_ERROR = 0x2
FLAG_TRIGGER_ACTION = 0x4000

CHUNK_END = 0
CHUNK_OUT = 1
CHUNK_ERR = 2

STRING_INPUT = 1


def die(msg):
    print("PROBE_CSQL FAIL: " + msg)
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
    header[5] = 3
    header[6] = CAS_PROTO_INDICATOR | PROTO_V12
    return bytes(header)


def db_info(dbname, dbuser, dbpasswd):
    buf = bytearray(DB_INFO_SIZE)
    buf[0:len(dbname)] = dbname.encode()
    buf[32:32 + len(dbuser)] = dbuser.encode()
    buf[64:64 + len(dbpasswd)] = dbpasswd.encode()
    url = b"probe_csql"
    buf[96:96 + len(url)] = url
    return bytes(buf)


def open_and_connect(port, dbname, dbuser, dbpasswd):
    sock = socket.create_connection(("127.0.0.1", port), timeout=30)
    sock.sendall(connect_header())
    ack = struct.unpack(">i", recv_exact(sock, 4, "broker ack"))[0]
    if ack != 0:
        die("broker ack expected 0, got %d" % ack)
    sock.sendall(db_info(dbname, dbuser, dbpasswd))
    length = struct.unpack(">i", recv_exact(sock, 4, "reply length"))[0]
    recv_exact(sock, 4, "cas_info")
    if length < 0:
        die("connect refused (length %d)" % length)
    recv_exact(sock, length, "connect reply body")
    if length != 36:
        die("connect reply length expected 36, got %d" % length)
    return sock


def arg_int(v):
    return struct.pack(">i", 4) + struct.pack(">i", v)


def arg_str(s):
    b = s.encode() + b"\x00"
    return struct.pack(">i", len(b)) + b


def send_request(sock, body):
    sock.sendall(struct.pack(">i", len(body)) + b"\xff\xff\xff\xff" + body)
    length = struct.unpack(">i", recv_exact(sock, 4, "reply length"))[0]
    recv_exact(sock, 4, "reply cas_info")
    return recv_exact(sock, length, "reply body")


def parse_reply(body, what):
    if len(body) < 8:
        die("%s: reply too short (%d bytes)" % (what, len(body)))
    result_code = struct.unpack(">i", body[0:4])[0]
    if result_code < 0:
        die("%s: server error frame, code %d" % (what, result_code))
    status = struct.unpack(">i", body[4:8])[0]
    # body[8] is the tran-dirty byte (db_commit_is_needed), skipped here
    pos = 9
    chunks = []
    while True:
        if pos >= len(body):
            die("%s: reply ended without CHUNK_END" % what)
        tag = body[pos]
        pos += 1
        if tag == CHUNK_END:
            break
        clen = struct.unpack(">i", body[pos:pos + 4])[0]
        pos += 4
        chunks.append((tag, body[pos:pos + clen]))
        pos += clen
    return status, chunks


def text_of(chunks, tag):
    return b"".join(c for t, c in chunks if t == tag).decode("utf-8", "replace")


def csql_execute(sock, text, flags):
    body = struct.pack(">b", CAS_FC_CSQL_REQUEST)
    body += arg_int(SUB_EXECUTE)
    body += arg_int(flags)
    body += arg_int(STRING_INPUT)
    body += arg_int(-1)          # line_no
    body += arg_int(0)           # string_width
    body += arg_str("")          # delimiters
    body += arg_str("")          # column widths
    body += arg_str("")          # in_file_name
    body += arg_str(text)
    return parse_reply(send_request(sock, body), "execute %r" % text)


def csql_session_cmd(sock, line, flags):
    body = struct.pack(">b", CAS_FC_CSQL_REQUEST)
    body += arg_int(SUB_SESSION_CMD)
    body += arg_int(flags)
    body += arg_int(0)           # string_width
    body += arg_str("")          # column widths
    body += arg_str(line)
    return parse_reply(send_request(sock, body), "session_cmd %r" % line)


def main():
    if len(sys.argv) != 5:
        die("usage: probe_csql.py <broker_port> <dbname> <dbuser> <dbpasswd>")
    port = int(sys.argv[1])
    dbname, dbuser, dbpasswd = sys.argv[2], sys.argv[3], sys.argv[4]

    sock = open_and_connect(port, dbname, dbuser, dbpasswd)
    print("PROBE_CSQL: connected 1-hop")

    # 1. plain SELECT renders like fat csql
    status, chunks = csql_execute(sock, "SELECT 1;", FLAG_AUTO_COMMIT | FLAG_TRIGGER_ACTION)
    out = text_of(chunks, CHUNK_OUT)
    if status != 0:
        die("SELECT 1 status %d, out=%r err=%r" % (status, out, text_of(chunks, CHUNK_ERR)))
    if "=== " not in out or "row selected" not in out:
        die("SELECT 1 rendering missing banner/rowcount: %r" % out)
    print("PROBE_CSQL: SELECT 1 rendered (%d bytes)" % len(out))

    # 2. syntax error goes to the ERR stream, session survives
    status, chunks = csql_execute(sock, "SELEC 1;", FLAG_AUTO_COMMIT | FLAG_CONTINUE_ON_ERROR | FLAG_TRIGGER_ACTION)
    err = text_of(chunks, CHUNK_ERR)
    if status <= 0:
        die("syntax error not reported as failure (status %d)" % status)
    if "ERROR" not in err and "error" not in err:
        die("syntax error text missing on ERR stream: %r" % err)
    print("PROBE_CSQL: syntax error captured on ERR stream")

    # 3. still usable after the error
    status, chunks = csql_execute(sock, "SELECT COUNT(*) FROM db_class;", FLAG_AUTO_COMMIT | FLAG_TRIGGER_ACTION)
    if status != 0 or "row selected" not in text_of(chunks, CHUNK_OUT):
        die("post-error execute failed: status %d out=%r err=%r"
            % (status, text_of(chunks, CHUNK_OUT), text_of(chunks, CHUNK_ERR)))
    print("PROBE_CSQL: session healthy after statement error")

    # 4. server-dependent session command renders
    status, chunks = csql_session_cmd(sock, ";schema db_class", FLAG_AUTO_COMMIT | FLAG_TRIGGER_ACTION)
    out = text_of(chunks, CHUNK_OUT)
    if status != 0 or "db_class" not in out:
        die("schema command failed: status %d out=%r err=%r"
            % (status, out, text_of(chunks, CHUNK_ERR)))
    print("PROBE_CSQL: ;schema rendered (%d bytes)" % len(out))

    # 5. client-only command (;shell would exec csh in fat csql) is refused
    #    by the SERVER_MODE allowlist gate
    status, chunks = csql_session_cmd(sock, ";shell", FLAG_AUTO_COMMIT | FLAG_TRIGGER_ACTION)
    if status == 0:
        die("client-only command was accepted server-side")
    print("PROBE_CSQL: client-only ;shell refused (status %d)" % status)

    sock.close()
    print("PROBE_CSQL: SUCCESS")


if __name__ == "__main__":
    main()
