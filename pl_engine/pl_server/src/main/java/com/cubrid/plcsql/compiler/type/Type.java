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

import java.util.Map;
import java.util.HashMap;

public class Type {

    public final int idx;

    public final String plcName;
    public final String fullJavaType;
    public final String typicalValueStr;
    public final String javaCode;

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
        return idx == IDX_DATE
                || idx == IDX_TIME
                || idx == IDX_DATETIME
                || idx == IDX_TIMESTAMP;
    }

    protected Type(int idx, String plcName, String fullJavaType, String typicalValueStr) {
        this.idx = idx;
        this.plcName = plcName;
        this.fullJavaType = fullJavaType;
        this.typicalValueStr = typicalValueStr;
        this.javaCode = getJavaCode(fullJavaType);
    }

    public static final int INVALID_IDX = 0;
    // types used only by the typechecker
    public static final int IDX_CURSOR = 1;
    public static final int IDX_NULL = 2;
    // types used by users and SpLib
    public static final int IDX_OBJECT = 3;
    public static final int IDX_BOOLEAN = 4;
    public static final int IDX_STRING = 5;
    public static final int IDX_SHORT = 6;
    public static final int IDX_INT = 7;
    public static final int IDX_BIGINT = 8;
    public static final int IDX_NUMERIC = 9;
    public static final int IDX_FLOAT = 10;
    public static final int IDX_DOUBLE = 11;
    public static final int IDX_DATE = 12;
    public static final int IDX_TIME = 13;
    public static final int IDX_DATETIME = 14;
    public static final int IDX_TIMESTAMP = 15;
    public static final int IDX_SYS_REFCURSOR = 16;

    public static final int FIRST_IDX = IDX_CURSOR;
    public static final int BOUND_OF_IDX = IDX_SYS_REFCURSOR + 1;

    // the following two are not actual Java types but only for internal type checking
    public static Type CURSOR = new Type(IDX_CURSOR,
        "Cursor",
        "Cursor",
        null);
    public static Type NULL = new Type(IDX_NULL,
        "Null",
        "Null",
        "null");

    // (1) used as an argument type of some operators in SpLib
    // (2) used as an expression type when a specific Java type cannot be given
    public static Type OBJECT = new Type(IDX_OBJECT,
        "Object",
        "java.lang.Object",
        "?");

    public static Type BOOLEAN = new Type(IDX_BOOLEAN,
        "Boolean",
        "java.lang.Boolean",
        null);
    // CHAR or VARCHAR with any length
    public static Type STRING_ANY = new Type(IDX_STRING,
        "String",
        "java.lang.String",
        "cast(? as string)");
    // NUMERIC with any precision and scale
    public static Type NUMERIC_ANY = new Type(IDX_NUMERIC,
        "Numeric",
        "java.math.BigDecimal",
        "cast(? as numeric)");
    public static Type SHORT = new Type(IDX_SHORT,
        "Short",
        "java.lang.Short",
        "cast(? as short)");
    public static Type INT = new Type(IDX_INT,
        "Int",
        "java.lang.Integer",
        "cast(? as int)");
    public static Type BIGINT = new Type(IDX_BIGINT,
        "Bigint",
        "java.lang.Long",
        "cast(? as bigint)");
    public static Type FLOAT = new Type(IDX_FLOAT,
        "Float",
        "java.lang.Float",
        "cast(? as float)");
    public static Type DOUBLE = new Type(IDX_DOUBLE,
        "Double",
        "java.lang.Double",
        "cast(? as double)");
    public static Type DATE = new Type(IDX_DATE,
        "Date",
        "java.sql.Date",
        "cast(? as date)");
    public static Type TIME = new Type(IDX_TIME,
        "Time",
        "java.sql.Time",
        "cast(? as time)");
    public static Type TIMESTAMP = new Type(IDX_TIMESTAMP,
        "Timestamp",
        "java.sql.Timestamp",
        "cast(? as timestamp)");
    public static Type DATETIME = new Type(IDX_DATETIME,
        "Datetime",
        "java.sql.Timestamp",
        "cast(? as datetime)");
    public static Type SYS_REFCURSOR = new Type(IDX_SYS_REFCURSOR,
        "Sys_refcursor",
        "com.cubrid.plcsql.predefined.sp.SpLib.Query",
        null);

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

        // instead of register(TIMESTAMP), a trick is necessary because DATETIME uses the same java.time.Timestamp;
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
        } else if (fullJavaType.equals("Cursor")) {
            return "%ERROR%";
        }

        // normal types
        String[] split = fullJavaType.split("\\.");
        assert split.length > 2;
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
                return TypeVariadic.getInstance(elem);
            }
        } else {
            return javaNameToType.get(javaName);
        }
    }

    public static boolean isUserType(Type ty) {
        return (ty.idx >= IDX_BOOLEAN);
    }
}
