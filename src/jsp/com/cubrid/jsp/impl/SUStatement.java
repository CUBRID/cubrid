package com.cubrid.jsp.impl;

import com.cubrid.jsp.data.CallInfo;
import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.ExecuteInfo;
import com.cubrid.jsp.data.FetchInfo;
import com.cubrid.jsp.data.GetByOIDInfo;
import com.cubrid.jsp.data.GetGeneratedKeysInfo;
import com.cubrid.jsp.data.GetSchemaInfo;
import com.cubrid.jsp.data.MakeOutResultSetInfo;
import com.cubrid.jsp.data.PrepareInfo;
import com.cubrid.jsp.data.QueryResultInfo;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConstants;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorCode;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorManager;
import com.cubrid.jsp.jdbc.CUBRIDServerSideOID;
import com.cubrid.jsp.value.ResultSetValue;
import com.cubrid.jsp.value.Value;
import cubrid.jdbc.jci.CUBRIDCommandType;
import cubrid.sql.CUBRIDOID;
import java.io.IOException;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class SUStatement {
    public static final int CURSOR_SET = 0, CURSOR_CUR = 1, CURSOR_END = 2;
    private static final byte NORMAL = 0,
            GET_BY_OID = 1,
            GET_SCHEMA_INFO = 2,
            GET_AUTOINCREMENT_KEYS = 3;

    private static final int DEFAULT_FETCH_SIZE = 100;

    private int handlerId = -1;
    private int type = NORMAL;

    /* prepare info */
    private String sqlStmt;
    private int columnNumber;
    private int parameterNumber;
    private byte commandType;
    private byte firstStmtType;

    private List<ColumnInfo> columnInfos = null;
    private HashMap<String, Integer> colNameToIndex = null;
    private SUBindParameter bindParameter = null;

    private byte executeFlag;
    private boolean isGeneratedKeys = false;
    private boolean isSensitive = false;

    /* execute info */
    private ExecuteInfo executeInfo = null;

    /* fetch info */
    private long queryId = -1;
    FetchInfo fetchInfo; // last fetched result
    private boolean wasNull = false;
    SUResultTuple tuples[] = null;

    /* related to fetch */
    private int maxFetchSize;
    private int fetchSize = DEFAULT_FETCH_SIZE;
    private int fetchDirection;

    int totalTupleNumber = 0; /* total */
    int cursorPosition = -1;

    int fetchedTupleNumber = 0;
    int fetchedStartCursorPosition = -1; /* start pos of fetched buffer */

    boolean isFetched = false;

    SUConnection suConn;

    public SUStatement(
            SUConnection conn, PrepareInfo info, boolean recompile, String sql, byte flag) {
        suConn = conn;

        sqlStmt = sql;

        maxFetchSize = 0;
        isSensitive = false;

        handlerId = info.handleId;
        commandType = info.stmtType;
        firstStmtType = commandType;

        /* init column infos */
        setColumnInfo(info.columnInfos);

        /* init bind paramter infos */
        parameterNumber = info.numParameters;
        bindParameter = new SUBindParameter(parameterNumber);

        /* init fetch infos */
        fetchSize = DEFAULT_FETCH_SIZE;
        fetchedStartCursorPosition = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
        fetchDirection = ResultSet.FETCH_FORWARD; // TODO: temporary init to FORWARD

        maxFetchSize = 0;
        isFetched = false;
        wasNull = false;

        if (commandType == CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
            columnNumber = parameterNumber + 1;
        }
    }

    public SUStatement(
            SUConnection conn, GetByOIDInfo info, CUBRIDOID oid, String attributeName[]) {
        suConn = conn;
        type = GET_BY_OID;
        handlerId = -1;

        fetchSize = 1;
        maxFetchSize = 0;
        isSensitive = false;

        columnNumber = info.columnInfos.size();

        fetchedStartCursorPosition = cursorPosition = 0;

        totalTupleNumber = 1;
        tuples = new SUResultTuple[totalTupleNumber];
        tuples[0] = new SUResultTuple(1, columnNumber);
        tuples[0].setOID(new SOID(oid.getOID()));

        for (int i = 0; i < columnNumber; i++) {
            tuples[0].setAttribute(i, info.dbValues.get(i));
        }
    }

    public SUStatement(
            SUConnection conn,
            GetSchemaInfo info,
            String cName,
            String attributePattern,
            int type) {
        type = GET_SCHEMA_INFO;
        handlerId = -1;

        fetchSize = 1;
        maxFetchSize = 0;
        isSensitive = false;
    }

    public SUStatement(SUConnection conn, GetGeneratedKeysInfo info) {
        suConn = conn;
        type = GET_AUTOINCREMENT_KEYS;

        /* init column infos */
        setColumnInfo(info.columnInfos);

        /* init fetch infos */
        fetchedStartCursorPosition = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
        fetchDirection = ResultSet.FETCH_FORWARD; // TODO: temporary init to FORWARD

        commandType = (byte) info.getResultInfo().stmtType;
        totalTupleNumber = info.getResultInfo().tupleCount;

        fetchInfo = info.getFetchInfo();

        handlerId = -1;
    }

    /* out resultset */
    public SUStatement(SUConnection conn, long queryId) throws IOException, SQLException {
        suConn = conn;
        type = NORMAL;

        this.queryId = queryId;

        MakeOutResultSetInfo info = suConn.makeOutResult(queryId);

        /* init column infos */
        setColumnInfo(info.columnInfos);

        totalTupleNumber = info.getResultInfo().tupleCount;
    }

    public boolean getSQLType() {
        switch (commandType) {
            case CUBRIDCommandType.CUBRID_STMT_SELECT:
            case CUBRIDCommandType.CUBRID_STMT_CALL:
            case CUBRIDCommandType.CUBRID_STMT_GET_STATS:
            case CUBRIDCommandType.CUBRID_STMT_EVALUATE:
                return true;
        }
        return false;
    }

    public void clearBindParameters() {
        if (bindParameter != null) {
            bindParameter.clear();
        }
    }

    public int getTotalTupleNumber() {
        return totalTupleNumber;
    }

    public boolean isOIDIncluded() {
        // TODO

        return false;
    }

    public void bindValue(int index, int type, Object data) throws SQLException {
        int bindIdx = index - 1;
        if (bindParameter == null || bindIdx < 0 || bindIdx >= parameterNumber) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_BIND_INDEX, null);
        }

        bindParameter.setParameter(bindIdx, type, data);
    }

    public void bindCollection(int index, Object values[]) throws SQLException {
        bindValue(index, DBType.DB_SEQUENCE, values);
    }

    public void execute(int maxRow, int maxField, boolean isSensitive, boolean isScrollable)
            throws IOException, SQLException {

        if (type == GET_SCHEMA_INFO) {
            return;
        }

        if (bindParameter != null && !bindParameter.checkAllBinded()) {
            // TODO: error handling
            return;
        }

        setExecuteFlags(maxRow, isSensitive);

        executeInfo = suConn.execute(handlerId, executeFlag, isScrollable, maxField, bindParameter);

        fetchedStartCursorPosition = cursorPosition = -1;

        if (firstStmtType == CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
            cursorPosition = 0; // already fetched
            fetchedStartCursorPosition = 0;
            fetchedTupleNumber = 1;

            CallInfo callInfo = executeInfo.callInfo;
            tuples = new SUResultTuple[fetchedTupleNumber];
            tuples[0] = callInfo.getTuple();
        } else if (getSQLType() == true) {
            queryId = executeInfo.getResultInfo().queryId;
        }

        totalTupleNumber = executeInfo.numAffected;
    }

    public Map<String, Integer> getColNameIndex() {
        return colNameToIndex;
    }

    private void setColumnInfo(List<ColumnInfo> infos) {
        columnInfos = infos;
        columnNumber = columnInfos.size();
        colNameToIndex = new HashMap<String, Integer>(columnNumber);
        for (int i = 0; i < columnInfos.size(); i++) {
            String name = columnInfos.get(i).colName.toLowerCase();
            if (colNameToIndex.containsKey(name) == false) {
                colNameToIndex.put(name, i);
            }
        }
    }

    public List<ColumnInfo> getColumnInfo() {
        if (columnInfos != null) {
            return columnInfos;
        }

        if (executeInfo == null) {
            return executeInfo.columnInfos;
        }
        return null;
    }

    private void setExecuteFlags(int maxRow, boolean isSensitive) {
        executeFlag = 0;

        if (isGeneratedKeys) {
            executeFlag |= CUBRIDServerSideConstants.EXEC_FLAG_GET_GENERATED_KEYS;
        }

        this.isSensitive = isSensitive;
        this.maxFetchSize = maxRow;
    }

    public void setAutoGeneratedKeys(boolean autoGeneratedKeys) {
        this.isGeneratedKeys = autoGeneratedKeys;
    }

    public byte getStatementType() {
        return commandType;
    }

    public boolean nextResult() throws SQLException {
        // TODO
        try {
            ExecuteInfo info = suConn.nextResult(handlerId);
        } catch (Exception e) {
            // TODO: error handling
            throw new SQLException(e);
        }
        return true;
    }

    public void fetch() throws SQLException {
        if (type == GET_BY_OID || type == GET_AUTOINCREMENT_KEYS) {
            return;
        }

        if (commandType == CUBRIDCommandType.CUBRID_STMT_CALL) {
            return;
        }

        /* need not to send fetch request */
        if (fetchedStartCursorPosition >= 0
                && fetchedStartCursorPosition <= cursorPosition
                && cursorPosition <= fetchedStartCursorPosition + fetchedTupleNumber) {
            return;
        }

        // send fetch request
        try {
            fetchInfo = suConn.fetch(queryId, cursorPosition, fetchSize, 0);
        } catch (IOException ioe) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, ioe);
        } catch (TypeMismatchException te) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ROW, te);
        }

        fetchedTupleNumber = fetchInfo.numFetched;
        fetchedStartCursorPosition = fetchInfo.tuples[0].tupleNumber() - 1;
    }

    public void moveCursor(int offset, int origin) {
        if ((origin != CURSOR_SET && origin != CURSOR_CUR && origin != CURSOR_END)
                || totalTupleNumber == 0) {
            // TODO: error handling
            return;
        }

        int currentCursor = cursorPosition;
        if (origin == CURSOR_SET) {
            cursorPosition = offset;
        } else if (origin == CURSOR_CUR) {
            cursorPosition += offset;
        }

        if (origin == CURSOR_END && totalTupleNumber != 0) {
            cursorPosition = totalTupleNumber - offset - 1;
            if (cursorPosition >= 0) return;
            else {
                // TODO: error handling
                cursorPosition = currentCursor;
                return;
            }
        }
    }

    public CUBRIDOID executeInsert(CUBRIDServerSideConnection con) throws SQLException {
        if (commandType != CUBRIDCommandType.CUBRID_STMT_INSERT) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_CMD_IS_NOT_INSERT, null);
        }

        try {
            execute(0, 0, false, false);
        } catch (IOException e) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, null);
        }

        if (executeInfo != null && executeInfo.getResultInfo() != null) {
            SOID oid = executeInfo.getResultInfo().getCUBRIDOID();
            return new CUBRIDServerSideOID(con, oid);
        }

        return null;
    }

    // ==============================================================
    // The following is to manage Result Info
    // ==============================================================

    public QueryResultInfo getResultInfo() {
        return executeInfo.getResultInfo();
    }

    // ==============================================================
    // The following is to get Result Tuple Values
    // ==============================================================

    private Object beforeGetTuple(int index) throws SQLException {
        if (index < 0 || index >= columnNumber) {
            CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COLUMN_INDEX, null);
        }

        if (type == NORMAL) {
            if (commandType == CUBRIDCommandType.CUBRID_STMT_CALL) {
                /* do nothing, tuples is already retrived when executing the call stmt */
            } else if (commandType != CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
                tuples = fetchInfo.tuples; // get tuples from fetchInfo
            }
        } else if (type == GET_AUTOINCREMENT_KEYS) {
            tuples = fetchInfo.tuples;
        } else {
            // GET_BY_OID initialized 1 tuple at constructor
        }

        Object obj;

        if ((tuples == null)
                || (tuples[cursorPosition - fetchedStartCursorPosition] == null)
                || ((obj = tuples[cursorPosition - fetchedStartCursorPosition].getAttribute(index))
                        == null)) {
            wasNull = true;
            return null;
        }
        wasNull = false;

        return obj;
    }

    public boolean getWasNull() {
        return wasNull;
    }

    public int getInt(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return 0;

        try {
            return obj.toInt();
        } catch (TypeMismatchException e) {
            return 0;
        }
    }

    public long getLong(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return 0;

        try {
            return obj.toLong();
        } catch (TypeMismatchException e) {
            return 0;
        }
    }

    public String getString(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return "";

        return obj.toString();
    }

    public float getFloat(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return 0.0f;

        try {
            return obj.toFloat();
        } catch (TypeMismatchException e) {
            return 0.0f;
        }
    }

    public double getDouble(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return 0.0;

        try {
            return obj.toDouble();
        } catch (TypeMismatchException e) {
            return 0.0;
        }
    }

    public short getShort(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return 0;

        try {
            return obj.toShort();
        } catch (TypeMismatchException e) {
            return 0;
        }
    }

    public boolean getBoolean(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;

        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return false;

        try {
            return (obj.toInt() == 1) ? true : false;
        } catch (TypeMismatchException e) {
            return false;
        }
    }

    public byte getByte(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return ((byte) 0);

        try {
            return obj.toByte();
        } catch (TypeMismatchException e) {
            return ((byte) 0);
        }
    }

    public byte[] getBytes(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toByteArray();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public BigDecimal getBigDecimal(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toBigDecimal();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public Date getDate(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toDate();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public Time getTime(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toTime();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public Timestamp getTimestamp(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toTimestamp();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public CUBRIDOID getColumnOID(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            return obj.toOid();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public CUBRIDOID getCursorOID() throws SQLException {

        /* fetch tuples including OID */
        fetch();

        tuples = fetchInfo.tuples;
        SUResultTuple currentTuple = null;

        if ((tuples == null)
                || (tuples[cursorPosition - fetchedStartCursorPosition] == null)
                || ((currentTuple = tuples[cursorPosition - fetchedStartCursorPosition]) == null)) {
            return null;
        }

        SOID soid = currentTuple.getOID();
        return new CUBRIDServerSideOID(suConn, soid);
    }

    public Object getCollection(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value obj = (Value) beforeGetTuple(idx);
        if (obj == null) return null;

        try {
            // TODO: check needed
            return obj.toObject();
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public Object getObject(int columnIndex) throws SQLException {
        int idx = columnIndex - 1;
        Value v = (Value) beforeGetTuple(idx);
        if (v == null) return null;

        // TODO: not implemented yet
        try {
            Object obj = null;
            if (v instanceof ResultSetValue) {
                obj = v.toResultSet(suConn);
            } else {
                obj = v.toObject();
            }
            return obj;
        } catch (TypeMismatchException e) {
            return null;
        }
    }

    public void registerOutParameter(int index, int sqlType) throws SQLException {
        int idx = index - 1;
        if (idx < 0 || idx >= parameterNumber) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_BIND_INDEX, null);
        }

        bindParameter.setOutParam(idx, sqlType);
    }

    public int getParameterCount() {
        return parameterNumber;
    }

    public int getFetchDirection() {
        return fetchDirection;
    }

    public int getFetchSize() {
        return fetchSize;
    }

    public int getColumnLength() {
        return columnNumber;
    }

    public long getQueryId() {
        return queryId;
    }

    public int getHandlerId() {
        return handlerId;
    }
}
