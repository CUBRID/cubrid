package com.cubrid.jsp.data;

public class DBParameterInfo {
    public int tran_isolation;
    public int wait_msec;

    public DBParameterInfo(CUBRIDUnpacker unpacker) {
        tran_isolation = unpacker.unpackInt();
        wait_msec = unpacker.unpackInt();
    }
}
