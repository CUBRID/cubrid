package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;

public class Header {

    public long sessionId; // DB SESSION ID
    public int code; // code

    /* for runtime */
    public int payloadSize = 0;
    public boolean hasPayload = false;

    public Header(CUBRIDUnpacker unpacker) {
        sessionId = unpacker.unpackBigint();
        code = unpacker.unpackInt();
    }

    public Header(long sessionId, int code, int size) {
        this.sessionId = sessionId;
        this.code = code;
    }

    @Override
    public String toString() {
        return "Header [sessionId=" + sessionId + ", code=" + code + "]";
    }
}
