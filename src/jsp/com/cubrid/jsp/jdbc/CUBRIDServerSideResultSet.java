/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.impl.SUConnection;
import com.cubrid.jsp.impl.SUStatement;
import cubrid.sql.CUBRIDOID;
import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;
import java.util.HashMap;
import java.util.Map;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideResultSet implements ResultSet {

    private CUBRIDServerSideConnection connection;
    private CUBRIDServerSideStatement stmt;
    private CUBRIDServerSideResultSetMetaData mdata;

    private SUStatement statementHandler;

    private int type = TYPE_FORWARD_ONLY;
    private int concurrency = CONCUR_READ_ONLY;

    /* For findColumn */
    protected HashMap<String, Integer> colNameToIdx;

    private boolean wasNullValue = false;

    private boolean isInserting;
    private int currentRowIndex = -1;

    protected CUBRIDServerSideResultSet(
            CUBRIDServerSideConnection c, CUBRIDServerSideStatement s, int t, int concur)
            throws SQLException {
        connection = c;
        stmt = s;

        setStatementHandler(stmt.getStatementHandler());

        type = t;
        concurrency = concur;

        isInserting = false;
    }

    protected CUBRIDServerSideResultSet(SUConnection ucon, long queryId)
            throws IOException, SQLException {
        setStatementHandler(new SUStatement(ucon, queryId));
    }

    protected CUBRIDServerSideResultSet(SUStatement stmt) {
        setStatementHandler(stmt);
    }

    public boolean isUpdatable() {
        return concurrency == CONCUR_UPDATABLE;
    }

    public boolean isScrollable() {
        return type != TYPE_FORWARD_ONLY;
    }

    public boolean isSensitive() {
        return type == TYPE_SCROLL_SENSITIVE;
    }

    private void move() {
        statementHandler.moveCursor(currentRowIndex, SUStatement.CURSOR_SET);
    }

    private void beforeGetValue(int columnIndex) throws SQLException {
        /* check row index is valid */
        checkRowIsValidForGet();

        /* check column is valid */
        checkColumnIsValid(columnIndex);

        /* check fetch is needed */
        statementHandler.fetch();
    }

    private void checkIsUpdatable() throws SQLException {
        // TODO: updatable is not implemented yet
        throw new SQLException(new UnsupportedOperationException());
    }

    private void checkIsSensitive() throws SQLException {
        // TODO: sensitive is not implemented yet
        throw new SQLException(new UnsupportedOperationException());
    }

    private void checkIsScrollable() throws SQLException {
        // TODO: scrollable is not implemented yet
        throw new SQLException(new UnsupportedOperationException());
    }

    private void checkRowIsValidForGet() throws SQLException {
        if (currentRowIndex == statementHandler.getTotalTupleNumber() || currentRowIndex == -1) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ROW, null);
        }
    }

    private void checkRowIsValidForUpdate() throws SQLException {
        if (currentRowIndex == statementHandler.getTotalTupleNumber() || currentRowIndex == -1) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ROW, null);
        }
    }

    private void checkColumnIsValid(int columnIndex) throws SQLException {
        if (columnIndex < 1 || columnIndex > statementHandler.getColumnLength()) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_INDEX, null);
        }
    }

    private void checkColumnIsUpdatable(int columnIndex) throws SQLException {
        // TODO: updatable is not implemented yet
        throw new SQLException(new UnsupportedOperationException());
        /*
         * if (updatable[columnIndex - 1] == false) { throw
         * con.createCUBRIDException(CUBRIDJDBCErrorCode.non_updatable_column, null); }
         */
    }

    protected void clearCurrentRow() throws SQLException {
        // TODO: clear for Streams (CLOB, BLOB, AsciiStream, BinaryStream)
        // TODO: clear for variables related to updatable
    }

    protected void setStatementHandler(SUStatement stmt) {
        statementHandler = stmt;
    }

    protected SUStatement getStatementHandler() {
        return statementHandler;
    }

    public long getQueryId() {
        return statementHandler.getQueryId();
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    @Override
    public boolean next() throws SQLException {
        if (statementHandler == null) {
            return false;
        }

        clearCurrentRow();
        currentRowIndex++;

        // check row index is exceeded total tuple number
        if (currentRowIndex >= statementHandler.getTotalTupleNumber()) {
            currentRowIndex = statementHandler.getTotalTupleNumber();
            return false;
        }

        // move cursor buffer
        move();
        isInserting = false;

        // TODO: do we need this?
        /*
         * if (error.getJdbcErrorCode() == UErrorCode.CAS_ER_NO_MORE_DATA) { return
         * false; }
         */

        return true;
    }

    @Override
    public void close() throws SQLException {
        /* NOTE: Server-side JDBC does not manage DB Resource */
        /* do nothing */
    }

    @Override
    public boolean wasNull() throws SQLException {
        return wasNullValue;
    }

    @Override
    public String getString(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getString(columnIndex);
    }

    @Override
    public boolean getBoolean(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getBoolean(columnIndex);
    }

    @Override
    public byte getByte(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getByte(columnIndex);
    }

    @Override
    public short getShort(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getShort(columnIndex);
    }

    @Override
    public int getInt(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getInt(columnIndex);
    }

    @Override
    public long getLong(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getLong(columnIndex);
    }

    @Override
    public float getFloat(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getFloat(columnIndex);
    }

    @Override
    public double getDouble(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getDouble(columnIndex);
    }

    @Override
    public BigDecimal getBigDecimal(int columnIndex, int scale) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public byte[] getBytes(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getBytes(columnIndex);
    }

    @Override
    public Date getDate(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getDate(columnIndex);
    }

    @Override
    public Time getTime(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getTime(columnIndex);
    }

    @Override
    public Timestamp getTimestamp(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getTimestamp(columnIndex);
    }

    @Override
    public InputStream getAsciiStream(int columnIndex) throws SQLException {
        // TODO: not implemented yet, related to CLOB, BLOB
        beforeGetValue(columnIndex);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public InputStream getUnicodeStream(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public InputStream getBinaryStream(int columnIndex) throws SQLException {
        // TODO: not implemented yet, related to CLOB, BLOB
        beforeGetValue(columnIndex);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public String getString(String columnName) throws SQLException {
        return getString(findColumn(columnName));
    }

    @Override
    public boolean getBoolean(String columnName) throws SQLException {
        return getBoolean(findColumn(columnName));
    }

    @Override
    public byte getByte(String columnName) throws SQLException {
        return getByte(findColumn(columnName));
    }

    @Override
    public short getShort(String columnName) throws SQLException {
        return getShort(findColumn(columnName));
    }

    @Override
    public int getInt(String columnName) throws SQLException {
        return getInt(findColumn(columnName));
    }

    @Override
    public long getLong(String columnName) throws SQLException {
        return getLong(findColumn(columnName));
    }

    @Override
    public float getFloat(String columnName) throws SQLException {
        return getFloat(findColumn(columnName));
    }

    @Override
    public double getDouble(String columnName) throws SQLException {
        return getDouble(findColumn(columnName));
    }

    @Override
    public BigDecimal getBigDecimal(String columnName, int scale) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public byte[] getBytes(String columnName) throws SQLException {
        return getBytes(findColumn(columnName));
    }

    @Override
    public Date getDate(String columnName) throws SQLException {
        return getDate(findColumn(columnName));
    }

    @Override
    public Time getTime(String columnName) throws SQLException {
        return getTime(findColumn(columnName));
    }

    @Override
    public Timestamp getTimestamp(String columnName) throws SQLException {
        return getTimestamp(findColumn(columnName));
    }

    @Override
    public InputStream getAsciiStream(String columnName) throws SQLException {
        return getAsciiStream(findColumn(columnName));
    }

    @Override
    public InputStream getUnicodeStream(String columnName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public InputStream getBinaryStream(String columnName) throws SQLException {
        return getBinaryStream(findColumn(columnName));
    }

    @Override
    public SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    @Override
    public void clearWarnings() throws SQLException {
        /* do nothing */
    }

    @Override
    public String getCursorName() throws SQLException {
        /* do nothing */
        return "";
    }

    @Override
    public ResultSetMetaData getMetaData() throws SQLException {
        if (mdata == null) {
            mdata = new CUBRIDServerSideResultSetMetaData(statementHandler);
        }
        return mdata;
    }

    @Override
    public Object getObject(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getObject(columnIndex);
    }

    @Override
    public Object getObject(String columnName) throws SQLException {
        return getObject(findColumn(columnName));
    }

    @Override
    public int findColumn(String columnName) throws SQLException {
        Integer index = statementHandler.getColNameIndex().get(columnName.toLowerCase());
        if (index == null) {
            CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_COLUMN_NAME, null);
        }

        return index.intValue() + 1;
    }

    @Override
    public Reader getCharacterStream(int columnIndex) throws SQLException {
        // TODO: not implemented yet, related to CLOB, BLOB
        beforeGetValue(columnIndex);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public Reader getCharacterStream(String columnName) throws SQLException {
        return getCharacterStream(findColumn(columnName));
    }

    @Override
    public BigDecimal getBigDecimal(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getBigDecimal(columnIndex);
    }

    @Override
    public BigDecimal getBigDecimal(String columnName) throws SQLException {
        return getBigDecimal(findColumn(columnName));
    }

    @Override
    public boolean isBeforeFirst() throws SQLException {
        if (statementHandler.getTotalTupleNumber() == 0) {
            return false;
        }
        return currentRowIndex == -1;
    }

    @Override
    public boolean isAfterLast() throws SQLException {
        if (statementHandler.getTotalTupleNumber() == 0) {
            return false;
        }
        return currentRowIndex == statementHandler.getTotalTupleNumber();
    }

    @Override
    public boolean isFirst() throws SQLException {
        if (statementHandler.getTotalTupleNumber() == 0) {
            return false;
        }
        return currentRowIndex == 0;
    }

    @Override
    public boolean isLast() throws SQLException {
        if (statementHandler.getTotalTupleNumber() == 0) {
            return false;
        }
        return currentRowIndex == statementHandler.getTotalTupleNumber() - 1;
    }

    @Override
    public void beforeFirst() throws SQLException {
        checkIsScrollable();
        clearCurrentRow();
        currentRowIndex = -1;
        isInserting = false;
    }

    @Override
    public void afterLast() throws SQLException {
        checkIsScrollable();
        clearCurrentRow();
        currentRowIndex = statementHandler.getTotalTupleNumber();
        isInserting = false;
    }

    @Override
    public boolean first() throws SQLException {
        checkIsScrollable();
        clearCurrentRow();
        isInserting = false;
        currentRowIndex = 0;
        if (statementHandler.getTotalTupleNumber() <= 0) {
            return false;
        }
        move();
        return true;
    }

    @Override
    public boolean last() throws SQLException {
        checkIsScrollable();
        clearCurrentRow();
        isInserting = false;
        currentRowIndex = statementHandler.getTotalTupleNumber() - 1;
        if (currentRowIndex < 0) {
            currentRowIndex = -1;
            return false;
        }
        move();
        return true;
    }

    @Override
    public int getRow() throws SQLException {
        if (currentRowIndex > -1 && currentRowIndex < statementHandler.getTotalTupleNumber()) {
            return currentRowIndex + 1;
        } else {
            return 0;
        }
    }

    @Override
    public boolean absolute(int row) throws SQLException {
        checkIsScrollable();
        clearCurrentRow();

        if (row == 0) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_ARGUMENT_ZERO, null);
        }

        int totalTupleNumber = statementHandler.getTotalTupleNumber();
        if (row > 0) {
            currentRowIndex = row - 1;
        } else {
            currentRowIndex = totalTupleNumber + row;
        }

        isInserting = false;

        if (currentRowIndex < 0) {
            currentRowIndex = -1;
            return false;
        }

        if (currentRowIndex >= totalTupleNumber) {
            currentRowIndex = totalTupleNumber;
            return false;
        }

        move();
        return true;
    }

    @Override
    public boolean relative(int rows) throws SQLException {
        checkIsScrollable();
        clearCurrentRow();

        int totalTupleNumber = statementHandler.getTotalTupleNumber();
        if (currentRowIndex == -1 || currentRowIndex == totalTupleNumber) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_ROW, null);
        }

        currentRowIndex += rows;
        isInserting = false;

        if (currentRowIndex < 0) {
            currentRowIndex = -1;
            return false;
        }

        if (currentRowIndex >= totalTupleNumber) {
            currentRowIndex = totalTupleNumber;
            return false;
        }

        move();
        return true;
    }

    @Override
    public boolean previous() throws SQLException {
        checkIsScrollable();
        clearCurrentRow();

        currentRowIndex--;
        isInserting = false;

        if (currentRowIndex < 0) {
            currentRowIndex = -1;
            return false;
        }

        move();
        return true;
    }

    @Override
    public void setFetchDirection(int direction) throws SQLException {
        // TODO: not implemented yet
        checkIsScrollable();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public int getFetchDirection() throws SQLException {
        return statementHandler.getFetchDirection();
    }

    @Override
    public void setFetchSize(int rows) throws SQLException {
        // TODO: not implemented yet
        if (rows < 0) {
            throw new IllegalArgumentException();
        }
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public int getFetchSize() throws SQLException {
        return statementHandler.getFetchSize();
    }

    @Override
    public int getType() throws SQLException {
        return type;
    }

    @Override
    public int getConcurrency() throws SQLException {
        return concurrency;
    }

    @Override
    public boolean rowUpdated() throws SQLException {
        checkIsSensitive();
        checkRowIsValidForGet();
        return false;
    }

    @Override
    public boolean rowInserted() throws SQLException {
        checkIsSensitive();
        checkRowIsValidForGet();
        return false;
    }

    @Override
    public boolean rowDeleted() throws SQLException {

        checkIsSensitive();
        checkRowIsValidForGet();

        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());

        /*
        boolean b = false;
        return b;
        */
    }

    private void updateValue(int columnIndex, Object value) throws SQLException {
        checkIsUpdatable();
        checkRowIsValidForUpdate();
        checkColumnIsValid(columnIndex);
        checkColumnIsUpdatable(columnIndex);

        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void updateNull(int columnIndex) throws SQLException {
        updateValue(columnIndex, null);
    }

    @Override
    public void updateBoolean(int columnIndex, boolean x) throws SQLException {
        updateValue(columnIndex, new Boolean(x));
    }

    @Override
    public void updateByte(int columnIndex, byte x) throws SQLException {
        updateValue(columnIndex, new Byte(x));
    }

    @Override
    public void updateShort(int columnIndex, short x) throws SQLException {
        updateValue(columnIndex, new Short(x));
    }

    @Override
    public void updateInt(int columnIndex, int x) throws SQLException {
        updateValue(columnIndex, new Integer(x));
    }

    @Override
    public void updateLong(int columnIndex, long x) throws SQLException {
        updateValue(columnIndex, new Long(x));
    }

    @Override
    public void updateFloat(int columnIndex, float x) throws SQLException {
        updateValue(columnIndex, new Float(x));
    }

    @Override
    public void updateDouble(int columnIndex, double x) throws SQLException {
        updateValue(columnIndex, new Double(x));
    }

    @Override
    public void updateBigDecimal(int columnIndex, BigDecimal x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateString(int columnIndex, String x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateBytes(int columnIndex, byte[] x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateDate(int columnIndex, Date x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateTime(int columnIndex, Time x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateTimestamp(int columnIndex, Timestamp x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateAsciiStream(int columnIndex, InputStream x, int length) throws SQLException {
        // TODO: not implemented yet
        // updateValue(columnIndex, new String(value, 0, len));
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void updateBinaryStream(int columnIndex, InputStream x, int length) throws SQLException {
        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void updateCharacterStream(int columnIndex, Reader x, int length) throws SQLException {
        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void updateObject(int columnIndex, Object x, int scale) throws SQLException {
        try {
            updateObject(columnIndex, new BigDecimal(((Number) x).toString()).setScale(scale));
        } catch (SQLException e) {
            throw e;
        } catch (Exception e) {
            updateObject(columnIndex, x);
        }
    }

    @Override
    public void updateObject(int columnIndex, Object x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateNull(String columnName) throws SQLException {
        updateNull(findColumn(columnName));
    }

    @Override
    public void updateBoolean(String columnName, boolean x) throws SQLException {
        updateBoolean(findColumn(columnName), x);
    }

    @Override
    public void updateByte(String columnName, byte x) throws SQLException {
        updateByte(findColumn(columnName), x);
    }

    @Override
    public void updateShort(String columnName, short x) throws SQLException {
        updateShort(findColumn(columnName), x);
    }

    @Override
    public void updateInt(String columnName, int x) throws SQLException {
        updateInt(findColumn(columnName), x);
    }

    @Override
    public void updateLong(String columnName, long x) throws SQLException {
        updateLong(findColumn(columnName), x);
    }

    @Override
    public void updateFloat(String columnName, float x) throws SQLException {
        updateFloat(findColumn(columnName), x);
    }

    @Override
    public void updateDouble(String columnName, double x) throws SQLException {
        updateDouble(findColumn(columnName), x);
    }

    @Override
    public void updateBigDecimal(String columnName, BigDecimal x) throws SQLException {
        updateBigDecimal(findColumn(columnName), x);
    }

    @Override
    public void updateString(String columnName, String x) throws SQLException {
        updateString(findColumn(columnName), x);
    }

    @Override
    public void updateBytes(String columnName, byte[] x) throws SQLException {
        updateBytes(findColumn(columnName), x);
    }

    @Override
    public void updateDate(String columnName, Date x) throws SQLException {
        updateDate(findColumn(columnName), x);
    }

    @Override
    public void updateTime(String columnName, Time x) throws SQLException {
        updateTime(findColumn(columnName), x);
    }

    @Override
    public void updateTimestamp(String columnName, Timestamp x) throws SQLException {
        updateTimestamp(findColumn(columnName), x);
    }

    @Override
    public void updateAsciiStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateAsciiStream(findColumn(columnName), x, length);
    }

    @Override
    public void updateBinaryStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateBinaryStream(findColumn(columnName), x, length);
    }

    @Override
    public void updateCharacterStream(String columnName, Reader reader, int length)
            throws SQLException {
        updateCharacterStream(findColumn(columnName), reader, length);
    }

    @Override
    public void updateObject(String columnName, Object x, int scale) throws SQLException {
        updateObject(findColumn(columnName), x, scale);
    }

    @Override
    public void updateObject(String columnName, Object x) throws SQLException {
        updateObject(findColumn(columnName), x);
    }

    @Override
    public void insertRow() throws SQLException {
        // TODO: not implemented yet
        checkIsUpdatable();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void updateRow() throws SQLException {
        // TODO: not implemented yet
        checkIsUpdatable();
        checkRowIsValidForGet();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void deleteRow() throws SQLException {
        // TODO: not implemented yet
        checkIsUpdatable();
        checkRowIsValidForGet();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void refreshRow() throws SQLException {
        // TODO: not implemented yet
        checkIsSensitive();
        checkRowIsValidForGet();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void cancelRowUpdates() throws SQLException {
        // TODO: not implemented yet
        checkIsUpdatable();
        checkRowIsValidForUpdate();
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public void moveToInsertRow() throws SQLException {
        checkIsUpdatable();
        clearCurrentRow();
        isInserting = true;
    }

    @Override
    public void moveToCurrentRow() throws SQLException {
        checkIsUpdatable();
        clearCurrentRow();
        isInserting = false;
    }

    @Override
    public Statement getStatement() throws SQLException {
        return stmt;
    }

    @Override
    public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Ref getRef(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Blob getBlob(int columnIndex) throws SQLException {
        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public Clob getClob(int columnIndex) throws SQLException {
        // TODO: not implemented yet
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    @Override
    public Array getArray(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Object getObject(String colName, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Ref getRef(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Blob getBlob(String colName) throws SQLException {
        return (getBlob(findColumn(colName)));
    }

    @Override
    public Clob getClob(String colName) throws SQLException {
        return (getClob(findColumn(colName)));
    }

    @Override
    public Array getArray(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public Date getDate(int columnIndex, Calendar cal) throws SQLException {
        return getDate(columnIndex);
    }

    @Override
    public Date getDate(String columnName, Calendar cal) throws SQLException {
        return getDate(columnName);
    }

    @Override
    public Time getTime(int columnIndex, Calendar cal) throws SQLException {
        return getTime(columnIndex);
    }

    @Override
    public Time getTime(String columnName, Calendar cal) throws SQLException {
        return getTime(columnName);
    }

    @Override
    public Timestamp getTimestamp(int columnIndex, Calendar cal) throws SQLException {
        return getTimestamp(columnIndex);
    }

    @Override
    public Timestamp getTimestamp(String columnName, Calendar cal) throws SQLException {
        return getTimestamp(columnName);
    }

    // 3.0
    @Override
    public URL getURL(int columnIndex) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public URL getURL(String columnName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public void updateArray(int columnIndex, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public void updateArray(String columnName, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public void updateBlob(int columnIndex, Blob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateBlob(String columnName, Blob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    @Override
    public void updateClob(int columnIndex, Clob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    @Override
    public void updateClob(String columnName, Clob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    @Override
    public void updateRef(int columnIndex, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    @Override
    public void updateRef(String columnName, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    // 3.0

    /**
     * Returns a <code>CUBRIDOID</code> object that represents the OID of the current cursor.
     *
     * @return the <code>CUBRIDOID</code> object of the current cursor if OID is included, <code>
     *     null</code> otherwise.
     * @exception SQLException if <code>this</code> object is closed.
     * @exception SQLException if the current cursor is on beforeFirst, afterLast or the insert row
     * @exception SQLException if a database access error occurs
     */
    public CUBRIDOID getOID(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getColumnOID(columnIndex);
    }

    public CUBRIDOID getOID(String columnName) throws SQLException {
        return getOID(findColumn(columnName));
    }

    public CUBRIDOID getOID() throws SQLException {
        CUBRIDOID value = statementHandler.getCursorOID();
        return value;
    }

    public Object getCollection(int columnIndex) throws SQLException {
        beforeGetValue(columnIndex);
        return statementHandler.getCollection(columnIndex);
    }

    public Object getCollection(String columnName) throws SQLException {
        return getCollection(findColumn(columnName));
    }

    /* JDK 1.6 */
    @Override
    public int getHoldability() throws SQLException {
        // NOTE: server-side JDBC does not support Holdable Cursor
        return HOLD_CURSORS_OVER_COMMIT; // default value;
    }

    /* JDK 1.6 */
    @Override
    public Reader getNCharacterStream(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public Reader getNCharacterStream(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public NClob getNClob(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public NClob getNClob(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public String getNString(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public String getNString(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public RowId getRowId(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public RowId getRowId(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public SQLXML getSQLXML(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public SQLXML getSQLXML(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public boolean isClosed() throws SQLException {
        /* NOTE: Server-side JDBC does not manage DB Resource */
        /* do nothing */
        return false;
    }

    /* JDK 1.6 */
    @Override
    public void updateAsciiStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateAsciiStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateAsciiStream(int columnIndex, InputStream x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateAsciiStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBinaryStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBinaryStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBinaryStream(int columnIndex, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBinaryStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBlob(int columnIndex, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBlob(String columnLabel, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBlob(int columnIndex, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateBlob(String columnLabel, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateClob(int columnIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(int columnIndex, NClob clob) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(String columnLabel, NClob clob) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(int columnIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNString(int columnIndex, String string) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateNString(String columnLabel, String string) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateRowId(int columnIndex, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateRowId(String columnLabel, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateSQLXML(int columnIndex, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    @Override
    public void updateSQLXML(String columnLabel, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    @Override
    public <T> T getObject(int columnIndex, Class<T> type) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    @Override
    public <T> T getObject(String columnLabel, Class<T> type) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
