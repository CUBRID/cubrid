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

import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.ColumnMeta;
import com.cubrid.jsp.impl.SUStatement;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideResultSetMetaData implements ResultSetMetaData {

    private List<ColumnInfo> columnInfos;
    private List<ColumnMeta> columnMetas;

    CUBRIDServerSideResultSetMetaData(SUStatement stmtHandler) {
        this.columnInfos = stmtHandler.getColumnInfo();

        columnMetas = new ArrayList<ColumnMeta>();
        for (int i = 0; i < columnInfos.size(); i++) {
            columnMetas.add(new ColumnMeta(columnInfos.get(i)));
        }
    }

    private void checkColumnIndex(int column) throws SQLException {
        if (column < 1 || column > columnInfos.size()) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_INVALID_INDEX, null);
        }
    }

    // ==============================================================
    // The following is JDBC Interface Implementations
    // ==============================================================

    @Override
    public int getColumnCount() throws SQLException {
        return columnInfos.size();
    }

    @Override
    public boolean isAutoIncrement(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).autoIncrement == 1;
    }

    @Override
    public boolean isCaseSensitive(int column) throws SQLException {
        checkColumnIndex(column);
        int type = columnInfos.get(column - 1).getColumnType();
        if (type == java.sql.Types.CHAR
                || type == java.sql.Types.VARCHAR
                || type == java.sql.Types.LONGVARCHAR) {
            return true;
        }
        return false;
    }

    @Override
    public boolean isSearchable(int column) throws SQLException {
        checkColumnIndex(column);
        return true;
    }

    @Override
    public boolean isCurrency(int column) throws SQLException {
        checkColumnIndex(column);
        int type = columnInfos.get(column - 1).getColumnType();
        if (type == java.sql.Types.DOUBLE
                || type == java.sql.Types.REAL
                || type == java.sql.Types.NUMERIC) {
            return true;
        }
        return false;
    }

    @Override
    public int isNullable(int column) throws SQLException {
        checkColumnIndex(column);
        return columnMetas.get(column - 1).getColumnNullable();
    }

    @Override
    public boolean isSigned(int column) throws SQLException {
        checkColumnIndex(column);
        int type = columnInfos.get(column - 1).getColumnType();
        if (type == java.sql.Types.SMALLINT
                || type == java.sql.Types.INTEGER
                || type == java.sql.Types.NUMERIC
                || type == java.sql.Types.DECIMAL
                || type == java.sql.Types.REAL
                || type == java.sql.Types.DOUBLE) {
            return true;
        }
        return false;
    }

    @Override
    public int getColumnDisplaySize(int column) throws SQLException {
        checkColumnIndex(column);
        return columnMetas.get(column - 1).getColumnDisplaySize();
    }

    @Override
    public String getColumnLabel(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getColumnName();
    }

    @Override
    public String getColumnName(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getColumnName();
    }

    @Override
    public String getSchemaName(int column) throws SQLException {
        checkColumnIndex(column);
        return "";
    }

    @Override
    public int getPrecision(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getColumnPrecision();
    }

    @Override
    public int getScale(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getColumnScale();
    }

    @Override
    public String getTableName(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getClassName();
    }

    @Override
    public String getCatalogName(int column) throws SQLException {
        checkColumnIndex(column);
        return "";
    }

    @Override
    public int getColumnType(int column) throws SQLException {
        checkColumnIndex(column);
        return columnMetas.get(column - 1).getColumnType();
    }

    @Override
    public String getColumnTypeName(int column) throws SQLException {
        checkColumnIndex(column);
        return columnMetas.get(column - 1).getColumnTypeName();
    }

    @Override
    public boolean isReadOnly(int column) throws SQLException {
        checkColumnIndex(column);
        return false;
    }

    @Override
    public boolean isWritable(int column) throws SQLException {
        checkColumnIndex(column);
        return true;
    }

    @Override
    public boolean isDefinitelyWritable(int column) throws SQLException {
        checkColumnIndex(column);
        return false;
    }

    @Override
    public String getColumnClassName(int column) throws SQLException {
        checkColumnIndex(column);
        return columnMetas.get(column - 1).getColumnClassName();
    }

    public int getElementType(int column) throws SQLException {
        checkColumnIndex(column);

        String type = getColumnTypeName(column);
        if (!type.equals("SET") && !type.equals("MULTISET") && !type.equals("SEQUENCE")) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_NOT_COLLECTION, null);
        }

        return columnMetas.get(column - 1).getElementType();
    }

    public String getElementTypeName(int column) throws SQLException {
        checkColumnIndex(column);

        String type = getColumnTypeName(column);
        if (!type.equals("SET") && !type.equals("MULTISET") && !type.equals("SEQUENCE")) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_NOT_COLLECTION, null);
        }

        return columnMetas.get(column - 1).getElementTypeName();
    }

    public String getColumnCharset(int column) throws SQLException {
        checkColumnIndex(column);
        return columnInfos.get(column - 1).getColumnCharsetName();
    }

    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> iface) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
