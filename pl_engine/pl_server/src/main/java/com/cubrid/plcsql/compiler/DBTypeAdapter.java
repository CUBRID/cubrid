/*
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

package com.cubrid.plcsql.compiler;

import com.cubrid.jsp.data.DBType;
import com.cubrid.plcsql.compiler.ast.TypeSpecSimple;

public class DBTypeAdapter {

    public static boolean isSupported(int dbType) {
        switch (dbType) {
            case DBType.DB_NULL:
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
            case DBType.DB_SHORT:
            case DBType.DB_INT:
            case DBType.DB_BIGINT:
            case DBType.DB_NUMERIC:
            case DBType.DB_FLOAT:
            case DBType.DB_DOUBLE:
            case DBType.DB_DATE:
            case DBType.DB_TIME:
            case DBType.DB_DATETIME:
            case DBType.DB_TIMESTAMP:
                return true;
        }

        return false;
    }

    public static TypeSpecSimple getTypeSpec(int dbType) {
        switch (dbType) {
            case DBType.DB_NULL:
                return TypeSpecSimple.NULL;
            case DBType.DB_CHAR:
            case DBType.DB_STRING:
                return TypeSpecSimple.STRING;
            case DBType.DB_SHORT:
                return TypeSpecSimple.SHORT;
            case DBType.DB_INT:
                return TypeSpecSimple.INT;
            case DBType.DB_BIGINT:
                return TypeSpecSimple.BIGINT;
            case DBType.DB_NUMERIC:
                return TypeSpecSimple.NUMERIC;
            case DBType.DB_FLOAT:
                return TypeSpecSimple.FLOAT;
            case DBType.DB_DOUBLE:
                return TypeSpecSimple.DOUBLE;
            case DBType.DB_DATE:
                return TypeSpecSimple.DATE;
            case DBType.DB_TIME:
                return TypeSpecSimple.TIME;
            case DBType.DB_DATETIME:
                return TypeSpecSimple.DATETIME;
            case DBType.DB_TIMESTAMP:
                return TypeSpecSimple.TIMESTAMP;
            default:
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
        }
    }

    public static String getSqlTypeName(int dbType) {

        switch (dbType) { // got from com.cubrid.jsp.data.DBType
            case DBType.DB_NULL:
                return "NULL";
            case DBType.DB_INT:
                return "INT";
            case DBType.DB_FLOAT:
                return "FLOAT";
            case DBType.DB_DOUBLE:
                return "DOUBLE";
            case DBType.DB_STRING:
                return "STRING";
            case DBType.DB_OBJECT:
                return "OBJECT";
            case DBType.DB_SET:
                return "SET";
            case DBType.DB_MULTISET:
                return "MULTISET";
            case DBType.DB_SEQUENCE:
                return "SEQUENCE";
            case DBType.DB_TIME:
                return "TIME";
            case DBType.DB_TIMESTAMP:
                return "TIMESTAMP";
            case DBType.DB_DATE:
                return "DATE";
            case DBType.DB_MONETARY:
                return "MONETARY";
            case DBType.DB_SHORT:
                return "SHORT";
            case DBType.DB_NUMERIC:
                return "NUMERIC";
            case DBType.DB_OID:
                return "OID";
            case DBType.DB_CHAR:
                return "CHAR";
            case DBType.DB_RESULTSET:
                return "RESULTSET";
            case DBType.DB_BIGINT:
                return "BIGINT";
            case DBType.DB_DATETIME:
                return "DATETIME";
            case DBType.DB_BIT:
                return "BIT";
            case DBType.DB_VARBIT:
                return "VARBIT";
            case DBType.DB_BLOB:
                return "BLOB";
            case DBType.DB_CLOB:
                return "CLOB";
            case DBType.DB_ENUMERATION:
                return "ENUMERATION";
            case DBType.DB_TIMESTAMPTZ:
                return "TIMESTAMPTZ";
            case DBType.DB_TIMESTAMPLTZ:
                return "TIMESTAMPLTZ";
            case DBType.DB_DATETIMETZ:
                return "DATETIMETZ";
            case DBType.DB_DATETIMELTZ:
                return "DATETIMELTZ";
            default:
                return "<Unknown>";
        }
    }
}
