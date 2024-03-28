package com.cubrid.jsp.data;

import java.util.ArrayList;
import java.util.List;

public class PrepareInfo {
    public int handleId;
    public byte stmtType;
    public int numParameters;
    public List<ColumnInfo> columnInfos = null;

    public PrepareInfo(CUBRIDUnpacker unpacker) {
        handleId = unpacker.unpackInt();
        stmtType = (byte) unpacker.unpackInt();
        numParameters = unpacker.unpackInt();

        int columnSize = unpacker.unpackInt();
        columnInfos = new ArrayList<ColumnInfo>();
        if (columnSize > 0) {
            for (int i = 0; i < columnSize; i++) {
                ColumnInfo cInfo = new ColumnInfo(unpacker);
                columnInfos.add(cInfo);
            }
        }
    }
}
