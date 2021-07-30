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

import cubrid.sql.CUBRIDOID;
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
import java.util.Map;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideResultSet implements ResultSet {

    CUBRIDServerSideConnection connection;
    CUBRIDServerSideStatement stmt;

    boolean isHoldable;

    protected CUBRIDServerSideResultSet(
        CUBRIDServerSideConnection c, 
        CUBRIDServerSideStatement s
    ) throws SQLException {
        connection = c;
        stmt = s;
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    public boolean next() throws SQLException {
        // TODO
        return true;
    }

    public void close() throws SQLException {
        // TODO
    }

    public boolean wasNull() throws SQLException {
        // TODO
        return true;
    }

    public String getString(int columnIndex) throws SQLException {
        // TODO
        return "dummy";
    }

    public boolean getBoolean(int columnIndex) throws SQLException {
        // TODO
        return true;
    }

    public byte getByte(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public short getShort(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public int getInt(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public long getLong(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public float getFloat(int columnIndex) throws SQLException {
        // TODO
        return 0.0f;
    }

    public double getDouble(int columnIndex) throws SQLException {
        // TODO
        return 0.0f;
    }

    public BigDecimal getBigDecimal(int columnIndex, int scale) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public byte[] getBytes(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Date getDate(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Time getTime(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Timestamp getTimestamp(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public InputStream getAsciiStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public InputStream getUnicodeStream(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public InputStream getBinaryStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public String getString(String columnName) throws SQLException {
        return getString(findColumn(columnName));
    }

    public boolean getBoolean(String columnName) throws SQLException {
        return getBoolean(findColumn(columnName));
    }

    public byte getByte(String columnName) throws SQLException {
        return getByte(findColumn(columnName));
    }

    public short getShort(String columnName) throws SQLException {
        return getShort(findColumn(columnName));
    }

    public int getInt(String columnName) throws SQLException {
        return getInt(findColumn(columnName));
    }

    public long getLong(String columnName) throws SQLException {
        return getLong(findColumn(columnName));
    }

    public float getFloat(String columnName) throws SQLException {
        return getFloat(findColumn(columnName));
    }

    public double getDouble(String columnName) throws SQLException {
        return getDouble(findColumn(columnName));
    }

    public BigDecimal getBigDecimal(String columnName, int scale) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public byte[] getBytes(String columnName) throws SQLException {
        return getBytes(findColumn(columnName));
    }

    public Date getDate(String columnName) throws SQLException {
        return getDate(findColumn(columnName));
    }

    public Time getTime(String columnName) throws SQLException {
        return getTime(findColumn(columnName));
    }

    public Timestamp getTimestamp(String columnName) throws SQLException {
        return getTimestamp(findColumn(columnName));
    }

    public InputStream getAsciiStream(String columnName) throws SQLException {
        return getAsciiStream(findColumn(columnName));
    }

    public InputStream getUnicodeStream(String columnName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public InputStream getBinaryStream(String columnName) throws SQLException {
        return getBinaryStream(findColumn(columnName));
    }

    public SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    public void clearWarnings() throws SQLException {
        /* do nothing */
    }

    public String getCursorName() throws SQLException {
        /* do nothing */
        return "";
    }

    public ResultSetMetaData getMetaData() throws SQLException {
        // TODO
        return null;
    }

    public Object getObject(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Object getObject(String columnName) throws SQLException {
        return getObject(findColumn(columnName));
    }

    //
    public int findColumn(String columnName) throws SQLException {
        // TODO
        return 0;
        /*
        Integer index = col_name_to_index.get(columnName.toLowerCase());
        if (index == null) {
            throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_column_name, null);
        }

        return index.intValue() + 1;
        */
    }

    public Reader getCharacterStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Reader getCharacterStream(String columnName) throws SQLException {
        return getCharacterStream(findColumn(columnName));
    }

    public BigDecimal getBigDecimal(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public BigDecimal getBigDecimal(String columnName) throws SQLException {
        return getBigDecimal(findColumn(columnName));
    }

    public boolean isBeforeFirst() throws SQLException {
        // TODO
        return true;
    }

    public boolean isAfterLast() throws SQLException {
        // TODO
        return true;
    }

    public boolean isFirst() throws SQLException {
        // TODO
        return true;
    }

    public boolean isLast() throws SQLException {
        // TODO
        return true;
    }

    public void beforeFirst() throws SQLException {
        // TODO
    }

    public void afterLast() throws SQLException {
        // TODO
    }

    public boolean first() throws SQLException {
        // TODO
        return true;
    }

    public boolean last() throws SQLException {
        // TODO
        return true;
    }

    public int getRow() throws SQLException {
        // TODO
        return 0;
    }

    public boolean absolute(int row) throws SQLException {
        // TODO
        return true;
    }

    public boolean relative(int rows) throws SQLException {
        // TODO
        return true;
    }

    public boolean previous() throws SQLException {
        // TODO
        return true;
    }

    public void setFetchDirection(int direction) throws SQLException {
        // TODO
    }

    public int getFetchDirection() throws SQLException {
        // TODO
        return 0;
    }

    public void setFetchSize(int rows) throws SQLException {
        // TODO
    }

    public int getFetchSize() throws SQLException {
        // TODO
        return 0;
    }

    public int getType() throws SQLException {
        // TODO
        return 0;
    }

    public int getConcurrency() throws SQLException {
        // TODO
        return 0;
    }

    public boolean rowUpdated() throws SQLException {
        // TODO
        return false;
    }

    public boolean rowInserted() throws SQLException {
        // TODO
        return false;
    }

    public boolean rowDeleted() throws SQLException {
        // TODO
        boolean b = false;
        return !b;
    }

    private void updateValue(int columnIndex, Object value) throws SQLException {
        // TODO
    }

    public void updateNull(int columnIndex) throws SQLException {
        updateValue(columnIndex, null);
    }

    public void updateBoolean(int columnIndex, boolean x) throws SQLException {
        updateValue(columnIndex, new Boolean(x));
    }

    public void updateByte(int columnIndex, byte x) throws SQLException {
        updateValue(columnIndex, new Byte(x));
    }

    public void updateShort(int columnIndex, short x) throws SQLException {
        updateValue(columnIndex, new Short(x));
    }

    public void updateInt(int columnIndex, int x) throws SQLException {
        updateValue(columnIndex, new Integer(x));
    }

    public void updateLong(int columnIndex, long x) throws SQLException {
        updateValue(columnIndex, new Long(x));
    }

    public void updateFloat(int columnIndex, float x) throws SQLException {
        updateValue(columnIndex, new Float(x));
    }

    public void updateDouble(int columnIndex, double x) throws SQLException {
        updateValue(columnIndex, new Double(x));
    }

    public void updateBigDecimal(int columnIndex, BigDecimal x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateString(int columnIndex, String x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateBytes(int columnIndex, byte[] x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateDate(int columnIndex, Date x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateTime(int columnIndex, Time x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateTimestamp(int columnIndex, Timestamp x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateAsciiStream(int columnIndex, InputStream x, int length)
            throws SQLException {
        // TODO
        // updateValue(columnIndex, new String(value, 0, len));
    }

    public void updateBinaryStream(int columnIndex, InputStream x, int length)
            throws SQLException {
        // TODO
    }

    public void updateCharacterStream(int columnIndex, Reader x, int length)
            throws SQLException {
        // TODO
    }

    public void updateObject(int columnIndex, Object x, int scale)
            throws SQLException {
        try {
            updateObject(columnIndex, new BigDecimal(((Number) x).toString()).setScale(scale));
        } catch (SQLException e) {
            throw e;
        } catch (Exception e) {
            updateObject(columnIndex, x);
        }
    }

    public void updateObject(int columnIndex, Object x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateNull(String columnName) throws SQLException {
        updateNull(findColumn(columnName));
    }

    public void updateBoolean(String columnName, boolean x) throws SQLException {
        updateBoolean(findColumn(columnName), x);
    }

    public void updateByte(String columnName, byte x) throws SQLException {
        updateByte(findColumn(columnName), x);
    }

    public void updateShort(String columnName, short x) throws SQLException {
        updateShort(findColumn(columnName), x);
    }

    public void updateInt(String columnName, int x) throws SQLException {
        updateInt(findColumn(columnName), x);
    }

    public void updateLong(String columnName, long x) throws SQLException {
        updateLong(findColumn(columnName), x);
    }

    public void updateFloat(String columnName, float x) throws SQLException {
        updateFloat(findColumn(columnName), x);
    }

    public void updateDouble(String columnName, double x) throws SQLException {
        updateDouble(findColumn(columnName), x);
    }

    public void updateBigDecimal(String columnName, BigDecimal x) throws SQLException {
        updateBigDecimal(findColumn(columnName), x);
    }

    public void updateString(String columnName, String x) throws SQLException {
        updateString(findColumn(columnName), x);
    }

    public void updateBytes(String columnName, byte[] x) throws SQLException {
        updateBytes(findColumn(columnName), x);
    }

    public void updateDate(String columnName, Date x) throws SQLException {
        updateDate(findColumn(columnName), x);
    }

    public void updateTime(String columnName, Time x) throws SQLException {
        updateTime(findColumn(columnName), x);
    }

    public void updateTimestamp(String columnName, Timestamp x) throws SQLException {
        updateTimestamp(findColumn(columnName), x);
    }

    public void updateAsciiStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateAsciiStream(findColumn(columnName), x, length);
    }

    public void updateBinaryStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateBinaryStream(findColumn(columnName), x, length);
    }

    public void updateCharacterStream(String columnName, Reader reader, int length)
            throws SQLException {
        updateCharacterStream(findColumn(columnName), reader, length);
    }

    public void updateObject(String columnName, Object x, int scale)
            throws SQLException {
        updateObject(findColumn(columnName), x, scale);
    }

    public void updateObject(String columnName, Object x) throws SQLException {
        updateObject(findColumn(columnName), x);
    }

    public void insertRow() throws SQLException {
        // TODO
    }

    public void updateRow() throws SQLException {
        // TODO
    }

    public void deleteRow() throws SQLException {
        // TODO
    }

    public void refreshRow() throws SQLException {
        // TODO
    }

    public void cancelRowUpdates() throws SQLException {
        // TODO
    }

    public void moveToInsertRow() throws SQLException {
        // TODO
    }

    public void moveToCurrentRow() throws SQLException {
        // TODO
    }

    public Statement getStatement() throws SQLException {
        return stmt;
    }

    public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Ref getRef(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Blob getBlob(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Clob getClob(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Array getArray(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Object getObject(String colName, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Ref getRef(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Blob getBlob(String colName) throws SQLException {
        return (getBlob(findColumn(colName)));
    }

    public Clob getClob(String colName) throws SQLException {
        return (getClob(findColumn(colName)));
    }

    public Array getArray(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Date getDate(int columnIndex, Calendar cal) throws SQLException {
        return getDate(columnIndex);
    }

    public Date getDate(String columnName, Calendar cal) throws SQLException {
        return getDate(columnName);
    }

    public Time getTime(int columnIndex, Calendar cal) throws SQLException {
        return getTime(columnIndex);
    }

    public Time getTime(String columnName, Calendar cal) throws SQLException {
        return getTime(columnName);
    }

    public Timestamp getTimestamp(int columnIndex, Calendar cal) throws SQLException {
        return getTimestamp(columnIndex);
    }

    public Timestamp getTimestamp(String columnName, Calendar cal)
            throws SQLException {
        return getTimestamp(columnName);
    }

    // 3.0
    public URL getURL(int columnIndex) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public URL getURL(String columnName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void updateArray(int columnIndex, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void updateArray(String columnName, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void updateBlob(int columnIndex, Blob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateBlob(String columnName, Blob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    public void updateClob(int columnIndex, Clob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public void updateClob(String columnName, Clob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    public void updateRef(int columnIndex, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void updateRef(String columnName, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    // 3.0

    public CUBRIDOID getOID(int columnIndex) throws SQLException {
        // TODO
        CUBRIDOID value = null;
        return value;
    }

    public CUBRIDOID getOID(String columnName) throws SQLException {
        return getOID(findColumn(columnName));
    }

    public Object getCollection(int columnIndex) throws SQLException {
        // TODO
        Object value = null;
        return value;
    }

    public Object getCollection(String columnName) throws SQLException {
        return getCollection(findColumn(columnName));
    }

    /**
     * Returns a <code>CUBRIDOID</code> object that represents the OID of the current cursor.
     *
     * @return the <code>CUBRIDOID</code> object of the current cursor if OID is included, <code>
     *     null</code> otherwise.
     * @exception SQLException if <code>this</code> object is closed.
     * @exception SQLException if the current cursor is on beforeFirst, afterLast or the insert row
     * @exception SQLException if a database access error occurs
     */
    public CUBRIDOID getOID() throws SQLException {
        // TODO
        CUBRIDOID value = null;
        return value;
    }

    /* JDK 1.6 */
    public int getHoldability() throws SQLException {
        return (isHoldable
                ? ResultSet.HOLD_CURSORS_OVER_COMMIT
                : ResultSet.CLOSE_CURSORS_AT_COMMIT);
    }

    /* JDK 1.6 */
    public Reader getNCharacterStream(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Reader getNCharacterStream(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public NClob getNClob(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public NClob getNClob(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public String getNString(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public String getNString(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public RowId getRowId(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public RowId getRowId(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML getSQLXML(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML getSQLXML(String columnLabel) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public boolean isClosed() throws SQLException {
        // TODO
        return true;
    }

    /* JDK 1.6 */
    public void updateAsciiStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(int columnIndex, InputStream x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(int columnIndex, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(int columnIndex, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(String columnLabel, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(int columnIndex, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(String columnLabel, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(int columnIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(int columnIndex, NClob clob) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(String columnLabel, NClob clob) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(int columnIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNString(int columnIndex, String string) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNString(String columnLabel, String string) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateRowId(int columnIndex, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateRowId(String columnLabel, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateSQLXML(int columnIndex, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
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
    public <T> T getObject(int columnIndex, Class<T> type) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public <T> T getObject(String columnLabel, Class<T> type) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
