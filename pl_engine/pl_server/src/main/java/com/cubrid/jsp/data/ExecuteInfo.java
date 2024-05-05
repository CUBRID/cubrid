package com.cubrid.jsp.data;

import java.util.ArrayList;
import java.util.List;

public class ExecuteInfo {

    public int numAffected;
    public QueryResultInfo qresultInfo = null;
    public List<ColumnInfo> columnInfos = null;
    public CallInfo callInfo = null;

    public ExecuteInfo(CUBRIDUnpacker unpacker) {
        numAffected = unpacker.unpackInt();

        qresultInfo = new QueryResultInfo(unpacker);

        int columnSize = unpacker.unpackInt();
        columnInfos = new ArrayList<ColumnInfo>();
        if (columnSize > 0) {
            for (int i = 0; i < columnSize; i++) {
                ColumnInfo cInfo = new ColumnInfo(unpacker);
                columnInfos.add(cInfo);
            }
        }

        boolean hasCallInfo = unpacker.unpackBool();
        if (hasCallInfo) {
            callInfo = new CallInfo(unpacker);
        }
    }

    public QueryResultInfo getResultInfo() {
        return qresultInfo;
    }

    public CallInfo getCallInfo() {
        return callInfo;
    }
}
