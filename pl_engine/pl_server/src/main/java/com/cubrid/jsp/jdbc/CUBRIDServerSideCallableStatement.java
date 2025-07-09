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

import cubrid.sql.CUBRIDTimestamptz;
import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.CallableStatement;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;
import java.util.Map;

public class CUBRIDServerSideCallableStatement extends CUBRIDServerSidePreparedStatement
        implements CallableStatement {

    protected CUBRIDServerSideCallableStatement(CUBRIDServerSideConnection c, String sql)
            throws SQLException {
        super(
                c,
                sql,
                ResultSet.TYPE_FORWARD_ONLY,
                ResultSet.CONCUR_READ_ONLY,
                ResultSet.CLOSE_CURSORS_AT_COMMIT,
                Statement.NO_GENERATED_KEYS);
    }

    @Override
    protected void prepareInternal(String sql) throws SQLException, IOException {
        byte prepareFlag = (byte) 0;

        prepareFlag |= CUBRIDServerSideConstants.PREPARE_CALL;
        if (isUpdatable() || isSensitive()) {
            prepareFlag |= CUBRIDServerSideConstants.PREPARE_UPDATABLE;
        }

        /*
         * NOTE: unsupported query_info and holdable cursor in server-side JDBC if
         * (false) { // query info prepareFlag |=
         * CUBRIDServerSideConstants.PREPARE_QUERY_INFO; } if (false) { // is_holdable
         * prepareFlag |= CUBRIDServerSideConstants.PREPARE_HOLDABLE; }
         */

        statementHandler = connection.getSUConnection().prepare(sql, prepareFlag, false);
    }

    private void beforeGetValue(int index) throws SQLException {
        if (index < 0 || index > statementHandler.getParameterCount()) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_INDEX, null);
        }

        /* check fetch is needed */
        statementHandler.fetch();

        // TODO: error handling?
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    public boolean wasNull() throws SQLException {
        return statementHandler.getWasNull();
    }

    public int getInt(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getInt(index);
    }

    public String getString(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getString(index);
    }

    public boolean getBoolean(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getBoolean(index);
    }

    public byte getByte(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getByte(index);
    }

    public short getShort(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getShort(index);
    }

    public long getLong(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getLong(index);
    }

    public float getFloat(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getFloat(index);
    }

    public double getDouble(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getDouble(index);
    }

    public byte[] getBytes(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getBytes(index);
    }

    public Date getDate(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getDate(index);
    }

    public Time getTime(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getTime(index);
    }

    public Timestamp getTimestamp(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getTimestamp(index);
    }

    public Object getObject(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getObject(index);
    }

    public BigDecimal getBigDecimal(int index) throws SQLException {
        beforeGetValue(index);
        return statementHandler.getBigDecimal(index);
    }

    public BigDecimal getBigDecimal(int index, int scale) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Ref getRef(int i) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Blob getBlob(int index) throws SQLException {
        // TODO: not implemented yet
        return null;
    }

    public Clob getClob(int index) throws SQLException {
        // TODO: not implemented yet
        return null;
    }

    public Array getArray(int i) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Date getDate(int index, Calendar cal) throws SQLException {
        return (getDate(index));
    }

    public Time getTime(int index, Calendar cal) throws SQLException {
        return (getTime(index));
    }

    public Timestamp getTimestamp(int index, Calendar cal) throws SQLException {
        return (getTimestamp(index));
    }

    public URL getURL(int index) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void setURL(String pName, URL val) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setNull(String pName, int sqlType) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBoolean(String pName, boolean x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setByte(String pName, byte x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setShort(String pName, short x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setInt(String pName, int x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setLong(String pName, long x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setFloat(String pName, float x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setDouble(String pName, double x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBigDecimal(String pName, BigDecimal x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setString(String pName, String x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBytes(String pName, byte[] x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setDate(String pName, Date x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTime(String pName, Time x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTimestamp(String pName, Timestamp x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTimestamptz(String pName, CUBRIDTimestamptz x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setAsciiStream(String pName, InputStream x, int length) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBinaryStream(String pName, InputStream x, int length) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setObject(String pName, Object x, int targetSqlType, int scale)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setObject(String pName, Object x, int targetSqlType) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setObject(String pName, Object x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setCharacterStream(String pName, Reader reader, int length) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setDate(String pName, Date x, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTime(String pName, Time x, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTimestamp(String pName, Timestamp x, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setTimestamptz(String pName, CUBRIDTimestamptz x, Calendar cal)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setNull(String pName, int sqlType, String typeName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public String getString(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public boolean getBoolean(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public byte getByte(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public short getShort(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public int getInt(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public long getLong(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public float getFloat(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public double getDouble(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public byte[] getBytes(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Date getDate(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Time getTime(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Timestamp getTimestamp(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Object getObject(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Object getObject(String pName, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public BigDecimal getBigDecimal(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Ref getRef(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Blob getBlob(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Clob getClob(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Array getArray(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Date getDate(String pName, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Time getTime(String pName, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Timestamp getTimestamp(String pName, Calendar cal) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public URL getURL(String pName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void registerOutParameter(int index, int sqlType) throws SQLException {
        statementHandler.registerOutParameter(index, sqlType);
    }

    public void registerOutParameter(int index, int sqlType, int scale) throws SQLException {
        statementHandler.registerOutParameter(index, sqlType);
    }

    public void registerOutParameter(int index, int sqlType, String typeName) throws SQLException {
        statementHandler.registerOutParameter(index, sqlType);
    }

    public void registerOutParameter(String pName, int sqlType) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void registerOutParameter(String pName, int sqlType, int scale) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void registerOutParameter(String pName, int sqlType, String typeName)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public int[] executeBatch() throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void addBatch() throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    /*
     * ======================================================================= |
     * PRIVATE METHODS
     * =======================================================================
     */

    /* JDK 1.6 */
    public Reader getCharacterStream(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Reader getCharacterStream(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Reader getNCharacterStream(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Reader getNCharacterStream(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public NClob getNClob(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public NClob getNClob(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public String getNString(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public String getNString(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public RowId getRowId(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public RowId getRowId(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML getSQLXML(int parameterIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML getSQLXML(String parameterName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setAsciiStream(String parameterName, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setAsciiStream(String parameterName, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBinaryStream(String parameterName, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBinaryStream(String parameterName, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBlob(String parameterName, Blob x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBlob(String parameterName, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBlob(String parameterName, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setCharacterStream(String parameterName, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setCharacterStream(String parameterName, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setClob(String parameterName, Clob x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setClob(String parameterName, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setClob(String parameterName, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNCharacterStream(String parameterName, Reader value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNCharacterStream(String parameterName, Reader value, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(String parameterName, NClob value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(String parameterName, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(String parameterName, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNString(String parameterName, String value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setRowId(String parameterName, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setSQLXML(String parameterName, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void closeOnCompletion() throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public boolean isCloseOnCompletion() throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public <T> T getObject(int parameterIndex, Class<T> type) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public <T> T getObject(String parameterName, Class<T> type) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }
}
