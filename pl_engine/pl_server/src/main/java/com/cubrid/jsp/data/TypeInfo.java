package com.cubrid.jsp.data;

public class TypeInfo {
    public int dbType;
    public short scale;
    public int prec;

    public TypeInfo() {} // for mock server API

    public TypeInfo(CUBRIDUnpacker unpacker) {
        dbType = unpacker.unpackInt();
        scale = unpacker.unpackShort();
        prec = unpacker.unpackInt();
    }
}
