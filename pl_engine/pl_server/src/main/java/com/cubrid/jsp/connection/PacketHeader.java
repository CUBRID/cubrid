package com.cubrid.jsp.connection;

public class PacketHeader {
    private int type = 0;
    private int version = 0;
    private int hostId = 0;
    private int transactionId = 0;
    private int requestId = 0;
    private int dbError = 0;
    private short functionCode = 0;
    private int flags = 0;
    private int bufferSize = 0;

    public void set(int type, short functionCode, int requestId, int bufferSize, int transactionid,
            int invalidateSnapshot, int dbError) {
        this.type = type;
        this.functionCode = functionCode;
        this.requestId = requestId;
        this.bufferSize = bufferSize;
        this.transactionId = transactionid;
        flags |= 0x8000;
    }
}
