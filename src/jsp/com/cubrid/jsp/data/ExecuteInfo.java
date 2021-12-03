package com.cubrid.jsp.data;

import java.util.ArrayList;
import java.util.List;

public class ExecuteInfo {

    public int numAffected;
    public List<QueryResultInfo> qresultInfos = null;
    public List<ColumnInfo> columnInfos = null;
    public CallInfo callInfo = null;

    public ExecuteInfo(CUBRIDUnpacker unpacker) {
        numAffected = unpacker.unpackInt();
        int numQueryResult = unpacker.unpackInt();
        qresultInfos = new ArrayList<QueryResultInfo>();
        if (numQueryResult > 0) {
            for (int i = 0; i < numQueryResult; i++) {
                QueryResultInfo qInfo = new QueryResultInfo(unpacker);
                qresultInfos.add(qInfo);
            }
        }

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

    public QueryResultInfo getResultInfo(int idx) {
        if (idx < 0 || idx >= qresultInfos.size()) {
            return null;
        }
        return qresultInfos.get(idx);
    }

    public int getResultInfoSize() {
        if (qresultInfos != null) {
            return qresultInfos.size();
        }
        return 0;
    }

    public CallInfo getCallInfo() {
        return callInfo;
    }
}
