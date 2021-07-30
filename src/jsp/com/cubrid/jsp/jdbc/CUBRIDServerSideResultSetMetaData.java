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

import java.sql.ResultSetMetaData;
import java.sql.SQLException;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideResultSetMetaData implements ResultSetMetaData {

    CUBRIDServerSideResultSetMetaData() {}

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    public int getColumnCount() throws SQLException {
        // TODO
        return 0;
    }

    public boolean isAutoIncrement(int column) throws SQLException {
        // TODO
        return true;
    }

    public boolean isCaseSensitive(int column) throws SQLException {
        // TODO
        return true;
    }

    public boolean isSearchable(int column) throws SQLException {
        // TODO
        return true;
    }

    public boolean isCurrency(int column) throws SQLException {
        // TODO
        return true;
    }

    public int isNullable(int column) throws SQLException {
        // TODO
        return 0;
    }

    public boolean isSigned(int column) throws SQLException {
        // TODO
        return true;
    }

    public int getColumnDisplaySize(int column) throws SQLException {
        // TODO
        return 0;
    }

    public String getColumnLabel(int column) throws SQLException {
        // TODO
        return "";
    }

    public String getColumnName(int column) throws SQLException {
        // TODO
        return "";
    }

    public String getSchemaName(int column) throws SQLException {
        // TODO
        return "";
    }

    public int getPrecision(int column) throws SQLException {
        // TODO
        return 0;
    }

    public int getScale(int column) throws SQLException {
        // TODO
        return 0;
    }

    public String getTableName(int column) throws SQLException {
        // TODO
        return "";
    }

    public String getCatalogName(int column) throws SQLException {
        // TODO
        return "";
    }

    public int getColumnType(int column) throws SQLException {
        // TODO
        return 0;
    }

    public String getColumnTypeName(int column) throws SQLException {
        // TODO
        return "";
    }

    public boolean isReadOnly(int column) throws SQLException {
        // TODO
        return true;
    }

    public boolean isWritable(int column) throws SQLException {
        // TODO
        return true;
    }

    public boolean isDefinitelyWritable(int column) throws SQLException {
        // TODO
        return true;
    }

    public String getColumnClassName(int column) throws SQLException {
        // TODO
        return "";
    }

    public int getElementType(int column) throws SQLException {
        // TODO
        return 0;
    }

    public String getElementTypeName(int column) throws SQLException {
        // TODO
        return "";
    }

    public String getColumnCharset(int column) throws SQLException {
        // TODO
        return "";
    }

    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    private void checkColumnIndex(int column) throws SQLException {
        // TODO
    }
}
