package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;

public class Header {

    public static final int EMPTY_SESSION_ID = 0;
    public static final int BYTES = getHeaderSize();

    public long id; // DB SESSION ID
    public int code; // code
    public int requestId; // request Id

    /* for runtime */
    public int payloadSize = 0;
    public boolean hasPayload = false;

    public Header(CUBRIDUnpacker unpacker) {
        id = unpacker.unpackBigint();
        code = unpacker.unpackInt();
        requestId = unpacker.unpackInt();
    }

    public Header(long id, int code, int size) {
        this.id = id;
        this.code = code;
        this.requestId = size;
    }

    @Override
    public String toString() {
        return "Header [id=" + id + ", code=" + code + ", rid=" + requestId + "]";
    }

    public static int getHeaderSize() {
        return 16;
    }
}
