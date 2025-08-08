package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;

public class Header {

    public long sessionId; // DB SESSION ID
    public int code; // code
    public int requestId; // request Id

    /* for runtime */
    public int payloadSize = 0;
    public boolean hasPayload = false;

    public Header(CUBRIDUnpacker unpacker) {
        sessionId = unpacker.unpackBigint();
        code = unpacker.unpackInt();
        requestId = unpacker.unpackInt();
    }

    public Header(long sessionId, int code, int size) {
        this.sessionId = sessionId;
        this.code = code;
        this.requestId = size;
    }

    @Override
    public String toString() {
        return "Header [sessionId=" + sessionId + ", code=" + code + ", rid=" + requestId + "]";
    }
}
