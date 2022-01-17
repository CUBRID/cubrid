package com.cubrid.jsp.data;

import java.util.ArrayList;
import java.util.List;

public class MakeOutResultSetInfo {
    public QueryResultInfo qresultInfo = null;
    public List<ColumnInfo> columnInfos = null;

    public MakeOutResultSetInfo(CUBRIDUnpacker unpacker) {
        qresultInfo = new QueryResultInfo(unpacker);

        int columnSize = unpacker.unpackInt();
        columnInfos = new ArrayList<ColumnInfo>();
        if (columnSize > 0) {
            for (int i = 0; i < columnSize; i++) {
                ColumnInfo cInfo = new ColumnInfo(unpacker);
                columnInfos.add(cInfo);
            }
        }
    }

    public QueryResultInfo getResultInfo() {
        return qresultInfo;
    }
}
