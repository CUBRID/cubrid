/*
 *
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

package com.cubrid.jsp.data;

import cubrid.jdbc.driver.CUBRIDBinaryString;
import cubrid.jdbc.driver.CUBRIDBlob;
import cubrid.jdbc.driver.CUBRIDClob;
import cubrid.sql.CUBRIDOID;
import cubrid.sql.CUBRIDTimestamp;
import cubrid.sql.CUBRIDTimestamptz;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;

public class DBType {
    public static final int DB_NULL = 0;
    public static final int DB_INT = 1;
    public static final int DB_FLOAT = 2;
    public static final int DB_DOUBLE = 3;
    public static final int DB_STRING = 4; /* DB_VARCHAR */
    public static final int DB_OBJECT = 5;
    public static final int DB_SET = 6;
    public static final int DB_MULTISET = 7;
    public static final int DB_SEQUENCE = 8;
    public static final int DB_TIME = 10;
    public static final int DB_TIMESTAMP = 11;
    public static final int DB_DATE = 12;
    public static final int DB_MONETARY = 13;
    public static final int DB_SHORT = 18;
    public static final int DB_NUMERIC = 22;
    public static final int DB_OID = 20;
    public static final int DB_CHAR = 25;
    public static final int DB_RESULTSET = 28;
    public static final int DB_BIGINT = 31;
    public static final int DB_DATETIME = 32;

    public static final int DB_BIT = 23;
    public static final int DB_VARBIT = 24;

    public static final int DB_BLOB = 33;
    public static final int DB_CLOB = 34;
    public static final int DB_ENUMERATION = 35;
    public static final int DB_TIMESTAMPTZ = 36;
    public static final int DB_TIMESTAMPLTZ = 37;
    public static final int DB_DATETIMETZ = 38;
    public static final int DB_DATETIMELTZ = 39;

    public static int getObjectDBtype(Object value) {
        if (value == null) return DB_NULL;
        else if (value instanceof String) return DB_STRING;
        else if (value instanceof Byte) return DB_SHORT;
        // else if (value instanceof byte[]) return DB_VARBIT;
        // else if (value instanceof Boolean) return DB_BIT;
        else if (value instanceof Short) return DB_SHORT;
        else if (value instanceof Integer) return DB_INT;
        else if (value instanceof Long) return DB_BIGINT;
        else if (value instanceof Double) return DB_DOUBLE;
        else if (value instanceof Float) return DB_FLOAT;
        else if (value instanceof BigDecimal || value instanceof Long) return DB_NUMERIC;
        else if (value instanceof Date) return DB_DATE;
        else if (value instanceof Time) return DB_TIME;
        else if (value instanceof CUBRIDTimestamptz) {
            if (CUBRIDTimestamp.isTimestampType((Timestamp) value)) {
                return DB_TIMESTAMPTZ;
            }
            return DB_DATETIMETZ;
        } else if (value instanceof Timestamp) {
            if (CUBRIDTimestamp.isTimestampType((Timestamp) value)) {
                return DB_TIMESTAMP;
            }
            return DB_DATETIME;
        } else if (value instanceof CUBRIDOID) return DB_OBJECT;
        else if (value instanceof CUBRIDBlob) return DB_BLOB;
        else if (value instanceof CUBRIDClob) return DB_CLOB;
        else if (value instanceof Object[]) return DB_SEQUENCE;
        else if (value instanceof CUBRIDBinaryString) return DB_STRING;
        else return DB_NULL;
    }

    public static String findFQDN(int type, int precesion, int collectionType) {
        switch (type) {
            case DBType.DB_NULL:
                return "null";
            case DBType.DB_BIT:
                return (precesion == 8) ? "java.lang.Boolean" : "byte[]";
            case DBType.DB_VARBIT:
                return "byte[]";
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                // case DBType.DB_NCHAR:
                // case DBType.DB_VARCHAR:
                // case DBType.DB_VARNCHAR:
                // case DBType.DB_ENUM:
                // case DBType.DB_JSON:
                return "java.lang.String";
            case DBType.DB_NUMERIC:
                return "java.math.BigDecimal";
            case DBType.DB_SHORT:
                return "java.lang.Short";
            case DBType.DB_INT:
                return "java.lang.Integer";
            case DBType.DB_BIGINT:
                return "java.lang.Long";
            case DBType.DB_FLOAT:
                return "java.lang.Float";
            case DBType.DB_MONETARY:
            case DBType.DB_DOUBLE:
                return "java.lang.Double";
            case DBType.DB_DATE:
                return "java.sql.Date";
            case DBType.DB_TIME:
                return "java.sql.Time";
            case DBType.DB_TIMESTAMP:
            case DBType.DB_DATETIME:
            case DBType.DB_TIMESTAMPTZ:
            case DBType.DB_TIMESTAMPLTZ:
            case DBType.DB_DATETIMETZ:
            case DBType.DB_DATETIMELTZ:
                return "java.sql.Timestamp";
            case DBType.DB_SET:
            case DBType.DB_SEQUENCE:
            case DBType.DB_MULTISET:
                break;
            case DBType.DB_OBJECT:
                return "cubrid.sql.CUBRIDOID";
            case DBType.DB_BLOB:
                return "java.sql.Blob";
            case DBType.DB_CLOB:
                return "java.sql.Clob";
            default:
                return "";
        }
        switch (collectionType) {
            case DBType.DB_NULL:
                return "null";
            case DBType.DB_BIT:
                return (precesion == 8) ? "java.lang.Boolean[]" : "byte[][]";
            case DBType.DB_VARBIT:
                return "byte[][]";
            case DBType.DB_CHAR:
                // case DBType.DB_NCHAR:
                // case DBType.DB_VARCHAR:
                // case DBType.DB_VARNCHAR:
                // case DBType.DB_ENUM:
                // case DBType.DB_JSON:
                return "java.lang.String[]";
            case DBType.DB_NUMERIC:
                return "java.lang.Double[]";
            case DBType.DB_SHORT:
                return "java.lang.Short[]";
            case DBType.DB_INT:
                return "java.lang.Integer[]";
            case DBType.DB_BIGINT:
                return "java.lang.Long[]";
            case DBType.DB_FLOAT:
                return "java.lang.Float[]";
            case DBType.DB_MONETARY:
            case DBType.DB_DOUBLE:
                return "java.lang.Double[]";
            case DBType.DB_DATE:
                return "java.sql.Date[]";
            case DBType.DB_TIME:
                return "java.sql.Time[]";
            case DBType.DB_TIMESTAMP:
            case DBType.DB_DATETIME:
            case DBType.DB_TIMESTAMPTZ:
            case DBType.DB_TIMESTAMPLTZ:
            case DBType.DB_DATETIMETZ:
            case DBType.DB_DATETIMELTZ:
                return "java.sql.Timestamp[]";
            case DBType.DB_SET:
            case DBType.DB_SEQUENCE:
            case DBType.DB_MULTISET:
                break;
            case DBType.DB_OBJECT:
                return "cubrid.sql.CUBRIDOID[]";
            case DBType.DB_BLOB:
                return "java.sql.Blob[]";
            case DBType.DB_CLOB:
                return "java.sql.Clob[]";
            default:
                break;
        }
        return null;
    }
}
