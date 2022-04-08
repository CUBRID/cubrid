package com.cubrid.jsp.data;

import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.DBType;
import java.sql.ResultSetMetaData;

public class ColumnMeta {
    private int columnType;
    private int elementType;
    private String columnTypeName;
    private String elementTypeName;
    private int columnDisplaySize;
    private int columnNull;
    private String columnClassName;

    public ColumnMeta(ColumnInfo info) {
        setColumnMeta(info);
    }

    /* getter */
    public int getColumnType() {
        return columnType;
    }

    public int getElementType() {
        return elementType;
    }

    public String getColumnTypeName() {
        return columnTypeName;
    }

    public String getElementTypeName() {
        return elementTypeName;
    }

    public int getColumnDisplaySize() {
        return columnDisplaySize;
    }

    public String getColumnClassName() {
        return columnClassName;
    }

    public int getColumnNullable() {
        return columnNull;
    }

    /* set variables from ColumnInfo */
    private void setElementType(ColumnInfo info) {
        switch (info.getCollectionType()) {
            case DBType.DB_CHAR:
                elementTypeName = "CHAR";
                elementType = java.sql.Types.CHAR;
                break;

            case DBType.DB_STRING:
                elementTypeName = "VARCHAR";
                elementType = java.sql.Types.VARCHAR;
                break;

            case DBType.DB_BIT:
                if (info.getColumnPrecision() == 8) {
                    elementTypeName = "BIT";
                    elementType = java.sql.Types.BIT;
                } else {
                    elementTypeName = "BIT";
                    elementType = java.sql.Types.BINARY;
                }
                break;

            case DBType.DB_VARBIT:
                elementTypeName = "BIT VARYING";
                elementType = java.sql.Types.VARBINARY;
                break;

            case DBType.DB_SHORT:
                elementTypeName = "SMALLINT";
                elementType = java.sql.Types.SMALLINT;
                break;

            case DBType.DB_INT:
                elementTypeName = "INTEGER";
                elementType = java.sql.Types.INTEGER;
                break;

            case DBType.DB_BIGINT:
                elementTypeName = "BIGINT";
                elementType = java.sql.Types.BIGINT;
                break;

            case DBType.DB_FLOAT:
                elementTypeName = "FLOAT";
                elementType = java.sql.Types.REAL;
                break;

            case DBType.DB_DOUBLE:
                elementTypeName = "DOUBLE";
                elementType = java.sql.Types.DOUBLE;
                break;

            case DBType.DB_MONETARY:
                elementTypeName = "MONETARY";
                elementType = java.sql.Types.DOUBLE;
                break;

            case DBType.DB_NUMERIC:
                elementTypeName = "NUMERIC";
                elementType = java.sql.Types.NUMERIC;
                break;

            case DBType.DB_DATE:
                elementTypeName = "DATE";
                elementType = java.sql.Types.DATE;
                break;

            case DBType.DB_TIME:
                elementTypeName = "TIME";
                elementType = java.sql.Types.TIME;
                break;

            case DBType.DB_TIMESTAMP:
                elementTypeName = "TIMESTAMP";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_TIMESTAMPTZ:
                elementTypeName = "TIMESTAMPTZ";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_TIMESTAMPLTZ:
                elementTypeName = "TIMESTAMPLTZ";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_DATETIME:
                elementTypeName = "DATETIME";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_DATETIMETZ:
                elementTypeName = "DATETIMETZ";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_DATETIMELTZ:
                elementTypeName = "DATETIMELTZ";
                elementType = java.sql.Types.TIMESTAMP;
                break;

            case DBType.DB_SET:
                elementTypeName = "SET";
                elementType = java.sql.Types.OTHER;
                break;

            case DBType.DB_MULTISET:
                elementTypeName = "MULTISET";
                elementType = java.sql.Types.OTHER;
                break;

            case DBType.DB_SEQUENCE:
                elementTypeName = "SEQUENCE";
                elementType = java.sql.Types.OTHER;
                break;

            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                elementTypeName = "CLASS";
                elementType = java.sql.Types.OTHER;
                break;

            case DBType.DB_NULL:
                elementTypeName = "";
                elementType = java.sql.Types.OTHER;
                break;

            case DBType.DB_ENUMERATION:
                elementTypeName = "ENUM";
                elementType = java.sql.Types.VARCHAR;
                break;

            default:
                // unknown type
                break;
        }
    }

    private void setColumnMeta(ColumnInfo info) {
        columnDisplaySize = getDefaultColumnDisplaySize(info.getColumnType());

        if (info.getIsNotNull()) {
            columnNull = ResultSetMetaData.columnNoNulls;
        } else {
            columnNull = ResultSetMetaData.columnNullable;
        }

        columnClassName =
                DBType.findFQDN(
                        info.getColumnType(), info.getColumnPrecision(), info.getCollectionType());

        switch (info.getColumnType()) {
            case DBType.DB_CHAR:
                columnTypeName = "CHAR";
                columnType = java.sql.Types.CHAR;
                elementType = -1;
                if (info.getColumnPrecision() > columnDisplaySize) {
                    columnDisplaySize = info.getColumnPrecision();
                }
                break;

            case DBType.DB_STRING:
                columnTypeName = "VARCHAR";
                columnType = java.sql.Types.VARCHAR;
                elementType = -1;
                if (info.getColumnPrecision() > columnDisplaySize) {
                    columnDisplaySize = info.getColumnPrecision();
                }
                break;

            case DBType.DB_BIT:
                if (info.getColumnPrecision() == 8) {
                    columnTypeName = "BIT";
                    columnType = java.sql.Types.BIT;
                } else {
                    columnTypeName = "BIT";
                    columnType = java.sql.Types.BINARY;
                }
                elementType = -1;
                if (info.getColumnPrecision() > columnDisplaySize) {
                    columnDisplaySize = info.getColumnPrecision();
                }
                break;

            case DBType.DB_VARBIT:
                columnTypeName = "BIT VARYING";
                columnType = java.sql.Types.VARBINARY;
                elementType = -1;
                if (info.getColumnPrecision() > columnDisplaySize) {
                    columnDisplaySize = info.getColumnPrecision();
                }
                break;

            case DBType.DB_SHORT:
                columnTypeName = "SMALLINT";
                columnType = java.sql.Types.SMALLINT;
                elementType = -1;
                break;

            case DBType.DB_INT:
                columnTypeName = "INTEGER";
                columnType = java.sql.Types.INTEGER;
                elementType = -1;
                break;

            case DBType.DB_BIGINT:
                columnTypeName = "BIGINT";
                columnType = java.sql.Types.BIGINT;
                elementType = -1;
                break;

            case DBType.DB_FLOAT:
                columnTypeName = "FLOAT";
                columnType = java.sql.Types.REAL;
                elementType = -1;
                break;

            case DBType.DB_DOUBLE:
                columnTypeName = "DOUBLE";
                columnType = java.sql.Types.DOUBLE;
                elementType = -1;
                break;

            case DBType.DB_MONETARY:
                columnTypeName = "MONETARY";
                columnType = java.sql.Types.DOUBLE;
                elementType = -1;
                break;

            case DBType.DB_NUMERIC:
                columnTypeName = "NUMERIC";
                columnType = java.sql.Types.NUMERIC;
                elementType = -1;
                break;

            case DBType.DB_DATE:
                columnTypeName = "DATE";
                columnType = java.sql.Types.DATE;
                elementType = -1;
                break;

            case DBType.DB_TIME:
                columnTypeName = "TIME";
                columnType = java.sql.Types.TIME;
                elementType = -1;
                break;

            case DBType.DB_TIMESTAMP:
                columnTypeName = "TIMESTAMP";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_TIMESTAMPTZ:
                columnTypeName = "TIMESTAMPTZ";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_TIMESTAMPLTZ:
                columnTypeName = "TIMESTAMPLTZ";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_DATETIME:
                columnTypeName = "DATETIME";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_DATETIMETZ:
                columnTypeName = "DATETIMETZ";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_DATETIMELTZ:
                columnTypeName = "DATETIMELTZ";
                columnType = java.sql.Types.TIMESTAMP;
                elementType = -1;
                break;

            case DBType.DB_SET:
                columnTypeName = "SET";
                columnType = java.sql.Types.OTHER;
                elementType = -1;
                setElementType(info);
                break;

            case DBType.DB_MULTISET:
                columnTypeName = "MULTISET";
                columnType = java.sql.Types.OTHER;
                elementType = -1;
                setElementType(info);
                break;

            case DBType.DB_SEQUENCE:
                columnTypeName = "SEQUENCE";
                columnType = java.sql.Types.OTHER;
                elementType = -1;
                setElementType(info);
                break;

            case DBType.DB_OID:
            case DBType.DB_OBJECT:
                columnTypeName = "CLASS";
                columnType = java.sql.Types.OTHER;
                elementType = -1;
                break;

            case DBType.DB_NULL:
                columnTypeName = "";
                columnType = java.sql.Types.OTHER;
                elementType = -1;
                break;

            case DBType.DB_ENUMERATION:
                columnTypeName = "ENUM";
                columnType = java.sql.Types.VARCHAR;
                elementType = -1;
                if (info.getColumnPrecision() > columnDisplaySize) {
                    columnDisplaySize = info.getColumnPrecision();
                }
                break;

            default:
                // unknown type
                break;
        }
    }

    private int getDefaultColumnDisplaySize(int type) {
        /* return default column display size based on column type */
        int size = -1;

        switch (type) {
            case DBType.DB_CHAR:
                size = 1;
                break;
            case DBType.DB_STRING:
                // case DBType.DB_ENUM:
                // case DBType.DB_JSON:
                size = 1;
                break;
            case DBType.DB_BIT:
                size = 1;
                break;
            case DBType.DB_VARBIT:
                size = 1;
                break;
            case DBType.DB_SHORT:
                size = 6;
                break;
            case DBType.DB_INT:
                size = 11;
                break;
            case DBType.DB_BIGINT:
                size = 20;
                break;
            case DBType.DB_FLOAT:
                size = 13;
                break;
            case DBType.DB_DOUBLE:
                size = 23;
                break;
            case DBType.DB_NUMERIC:
                size = 40;
                break;
            case DBType.DB_MONETARY:
                size = 23;
                break;
            case DBType.DB_DATE:
                size = 10;
                break;
            case DBType.DB_TIME:
                size = 8;
                break;
            case DBType.DB_TIMESTAMP:
                size = 19;
                break;
            case DBType.DB_TIMESTAMPTZ:
            case DBType.DB_TIMESTAMPLTZ:
                size = 19 + 63;
                break;
            case DBType.DB_DATETIME:
                size = 23;
                break;
            case DBType.DB_DATETIMETZ:
            case DBType.DB_DATETIMELTZ:
                size = 23 + 63;
                break;
            case DBType.DB_NULL:
                size = 4;
                break;
            case DBType.DB_OBJECT:
                size = 256;
                break;
            case DBType.DB_SET:
            case DBType.DB_MULTISET:
            case DBType.DB_SEQUENCE:
                size = -1;
                break;
            default:
                break;
        }
        return size;
    }
}
