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

package com.cubrid.plcsql.compiler.type;

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.value.NumericValue;
import com.cubrid.plcsql.compiler.serverapi.ServerConstants;
import java.util.HashMap;
import java.util.Map;

public class Type {

    public final int idx;

    public final String plcName;
    public final String fullJavaType;
    public final String typicalValueStr;
    public final String javaCode;
    public final int dbType;
    public final int prec;
    public final short scale;

    @Override
    public String toString() {
        return plcName;
    }

    @Override
    public boolean equals(Object that) {
        // Actually, this is the same as equals() method inherited from Object class.
        // I just want to be explicit.
        return this == that;
    }

    public boolean isNumber() {
        return idx == IDX_SHORT
                || idx == IDX_INT
                || idx == IDX_BIGINT
                || idx == IDX_NUMERIC
                || idx == IDX_FLOAT
                || idx == IDX_DOUBLE;
    }

    public boolean isString() {
        return idx == IDX_STRING;
    }

    public boolean isDateTime() {
        return idx == IDX_DATE || idx == IDX_TIME || idx == IDX_DATETIME || idx == IDX_TIMESTAMP;
    }

    protected Type(
            int idx,
            String plcName,
            String fullJavaType,
            String typicalValueStr,
            int dbType,
            int prec,
            short scale) {
        this.idx = idx;
        this.plcName = plcName;
        this.fullJavaType = fullJavaType;
        this.typicalValueStr = typicalValueStr;
        this.javaCode = getJavaCode(fullJavaType);
        this.dbType = dbType;
        this.prec = prec;
        this.scale = scale;
    }

    public static final int INVALID_IDX = 0;
    // types used only by the typechecker
    public static final int IDX_CURSOR = 1;
    public static final int IDX_NULL = 2;
    public static final int IDX_RECORD = 3;
    // types used by users and SpLib
    public static final int IDX_OBJECT = 4;
    public static final int IDX_BOOLEAN = 5;
    public static final int IDX_STRING = 6;
    public static final int IDX_SHORT = 7;
    public static final int IDX_INT = 8;
    public static final int IDX_BIGINT = 9;
    public static final int IDX_NUMERIC = 10;
    public static final int IDX_FLOAT = 11;
    public static final int IDX_DOUBLE = 12;
    public static final int IDX_DATE = 13;
    public static final int IDX_TIME = 14;
    public static final int IDX_DATETIME = 15;
    public static final int IDX_TIMESTAMP = 16;
    public static final int IDX_SYS_REFCURSOR = 17;
    public static final int BOUND_OF_IDX = 18;

    public static final int FIRST_IDX = IDX_CURSOR;

    protected static final int NO_DB_TYPE = -1;
    protected static final int NO_PREC = -1;
    protected static final short NO_SCALE = -1;

    // the following two are not actual Java types but only for internal type checking
    public static Type CURSOR =
            new Type(IDX_CURSOR, "Cursor", "Cursor", null, NO_DB_TYPE, NO_PREC, NO_SCALE);

    public static Type NULL =
            new Type(IDX_NULL, "Null", "Null", "null", DBType.DB_NULL, NO_PREC, NO_SCALE);

    public static Type RECORD_ANY =
            new Type(IDX_RECORD, "Record", "Record", null, NO_DB_TYPE, NO_PREC, NO_SCALE);

    // (1) used as an argument type of some operators in SpLib
    // (2) used as an expression type when a specific Java type cannot be given
    public static Type OBJECT =
            new Type(
                    IDX_OBJECT,
                    "Object",
                    "java.lang.Object",
                    "?",
                    DBType.DB_OBJECT,
                    NO_PREC,
                    NO_SCALE);

    public static Type BOOLEAN =
            new Type(
                    IDX_BOOLEAN,
                    "Boolean",
                    "java.lang.Boolean",
                    null,
                    NO_DB_TYPE,
                    NO_PREC,
                    NO_SCALE);
    // CHAR or VARCHAR with any length
    public static Type STRING_ANY =
            new Type(
                    IDX_STRING,
                    "String",
                    "java.lang.String",
                    "'xyz'",
                    DBType.DB_STRING,
                    ServerConstants.DB_DEFAULT_PRECISION,
                    ServerConstants.DB_DEFAULT_SCALE);
    // NUMERIC with any precision and scale
    public static Type NUMERIC_ANY =
            new Type(
                    IDX_NUMERIC,
                    "Numeric",
                    "java.math.BigDecimal",
                    "0.1",
                    DBType.DB_NUMERIC,
                    NumericValue.DB_DEFAULT_NUMERIC_PRECISION,
                    NumericValue.DB_DEFAULT_NUMERIC_SCALE);
    public static Type SHORT =
            new Type(
                    IDX_SHORT,
                    "Short",
                    "java.lang.Short",
                    "cast(1 as short)",
                    DBType.DB_SHORT,
                    NO_PREC,
                    NO_SCALE);
    public static Type INT =
            new Type(
                    IDX_INT,
                    "Int",
                    "java.lang.Integer",
                    "cast(1 as int)",
                    DBType.DB_INT,
                    NO_PREC,
                    NO_SCALE);
    public static Type BIGINT =
            new Type(
                    IDX_BIGINT,
                    "Bigint",
                    "java.lang.Long",
                    "cast(1 as bigint)",
                    DBType.DB_BIGINT,
                    NO_PREC,
                    NO_SCALE);
    public static Type FLOAT =
            new Type(
                    IDX_FLOAT,
                    "Float",
                    "java.lang.Float",
                    "cast(0.1 as float)",
                    DBType.DB_FLOAT,
                    NO_PREC,
                    NO_SCALE);
    public static Type DOUBLE =
            new Type(
                    IDX_DOUBLE,
                    "Double",
                    "java.lang.Double",
                    "cast(0.1 as double)",
                    DBType.DB_DOUBLE,
                    NO_PREC,
                    NO_SCALE);
    public static Type DATE =
            new Type(
                    IDX_DATE,
                    "Date",
                    "java.sql.Date",
                    "date'2000-10-10'",
                    DBType.DB_DATE,
                    NO_PREC,
                    NO_SCALE);
    public static Type TIME =
            new Type(
                    IDX_TIME,
                    "Time",
                    "java.sql.Time",
                    "time'13:14:15'",
                    DBType.DB_TIME,
                    NO_PREC,
                    NO_SCALE);
    public static Type TIMESTAMP =
            new Type(
                    IDX_TIMESTAMP,
                    "Timestamp",
                    "java.sql.Timestamp",
                    "timestamp'2000-10-10 13:14:15'",
                    DBType.DB_TIMESTAMP,
                    NO_PREC,
                    NO_SCALE);
    public static Type DATETIME =
            new Type(
                    IDX_DATETIME,
                    "Datetime",
                    "java.sql.Timestamp",
                    "datetime'2000-10-10 13:14:15.000'",
                    DBType.DB_DATETIME,
                    NO_PREC,
                    NO_SCALE);
    public static Type SYS_REFCURSOR =
            new Type(
                    IDX_SYS_REFCURSOR,
                    "Sys_refcursor",
                    "com.cubrid.plcsql.predefined.sp.SpLib.Query",
                    null,
                    NO_DB_TYPE,
                    NO_PREC,
                    NO_SCALE);

    private static final Map<String, Type> javaNameToType = new HashMap<>();
    private static final Map<Integer, Type> idxToType = new HashMap<>();

    private static void register(Type spec) {
        Type ty = javaNameToType.put(spec.fullJavaType, spec);
        assert ty == null;
        ty = idxToType.put(spec.idx, spec);
        assert ty == null;
    }

    static {
        register(OBJECT);
        register(BOOLEAN);
        register(STRING_ANY);
        register(NUMERIC_ANY);
        register(SHORT);
        register(INT);
        register(BIGINT);
        register(FLOAT);
        register(DOUBLE);
        register(DATE);
        register(TIME);

        // instead of register(TIMESTAMP), a trick is necessary because DATETIME uses the same
        // java.time.Timestamp;
        javaNameToType.put("java.time.ZonedDateTime", TIMESTAMP);
        idxToType.put(IDX_TIMESTAMP, TIMESTAMP);

        register(DATETIME);
        register(SYS_REFCURSOR);
    }

    public static Type getTypeByJavaName(String fullJavaType) {
        return javaNameToType.get(fullJavaType);
    }

    public static Type getTypeByIdx(int idx) {
        return idxToType.get(idx);
    }

    private static String getJavaCode(String fullJavaType) {

        if (fullJavaType == null) {
            return null;
        }

        // internal types
        if (fullJavaType.equals("Null")) {
            return "Object";
        } else if (fullJavaType.equals("Cursor") || fullJavaType.equals("Record")) {
            return "%ERROR%";
        }

        // normal types
        String[] split = fullJavaType.split("\\.");
        return split[split.length - 1];
    }

    public static Type ofJavaName(String javaName) {
        if (javaName.endsWith("[]")) {
            String elemJavaName = javaName.substring(0, javaName.length() - 2);
            Type elem = javaNameToType.get(elemJavaName);
            if (elem == null) {
                assert false : ("no type for a Java name " + javaName);
                return null;
            } else {
                return TypeVariadic.getStaticInstance(elem);
            }
        } else {
            return javaNameToType.get(javaName);
        }
    }

    public static boolean isUserType(Type ty) {
        return (ty instanceof TypeRecord) || (ty.idx >= IDX_BOOLEAN);
    }
}
