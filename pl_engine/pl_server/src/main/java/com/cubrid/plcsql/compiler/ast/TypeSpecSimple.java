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

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import java.util.HashMap;
import java.util.Map;

public class TypeSpecSimple extends TypeSpec {

    // types used only by the typechecker
    public static final int IDX_CURSOR = 0;
    public static final int IDX_NULL = 1;
    public static final int IDX_OBJECT = 2;

    // types used by users
    public static final int IDX_BOOLEAN = 3;
    public static final int IDX_STRING = 4;
    public static final int IDX_SHORT = 5;
    public static final int IDX_INT = 6;
    public static final int IDX_BIGINT = 7;
    public static final int IDX_NUMERIC = 8;
    public static final int IDX_FLOAT = 9;
    public static final int IDX_DOUBLE = 10;
    public static final int IDX_DATE = 11;
    public static final int IDX_TIME = 12;
    public static final int IDX_DATETIME = 13;
    public static final int IDX_TIMESTAMP = 14;
    public static final int IDX_SYS_REFCURSOR = 15;

    public static final int COUNT_OF_IDX = 16;

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitTypeSpecSimple(this);
    }

    public final String fullJavaType;

    public static boolean isUserType(TypeSpecSimple ty) {
        return (ty.simpleTypeIdx >= IDX_BOOLEAN);
    }

    public static TypeSpecSimple ofJavaName(String javaType) {
        TypeSpecSimple ret = javaNameToSpec.get(javaType);
        assert ret != null : "wrong Java type " + javaType;
        return ret;
    }

    @Override
    public String toJavaSignature() {
        return fullJavaType;
    }

    // the following two are not actual Java types but only for internal type checking
    public static TypeSpecSimple CURSOR = new TypeSpecSimple("Cursor", "Cursor", IDX_CURSOR, null);
    public static TypeSpecSimple NULL = new TypeSpecSimple("Null", "Null", IDX_NULL, "null");

    // (1) used as an argument type of some operators in SpLib
    // (2) used as an expression type when a specific Java type cannot be given
    public static TypeSpecSimple OBJECT =
            new TypeSpecSimple("Object", "java.lang.Object", IDX_OBJECT, "?");

    public static TypeSpecSimple BOOLEAN =
            new TypeSpecSimple("Boolean", "java.lang.Boolean", IDX_BOOLEAN, null);
    public static TypeSpecSimple STRING =
            new TypeSpecSimple("String", "java.lang.String", IDX_STRING, "cast(? as string)");
    public static TypeSpecSimple NUMERIC_ANY =     // numeric with any precision and scale
            new TypeSpecSimple("Numeric", "java.math.BigDecimal", IDX_NUMERIC, "cast(? as numeric)");
    public static TypeSpecSimple SHORT =
            new TypeSpecSimple("Short", "java.lang.Short", IDX_SHORT, "cast(? as short)");
    public static TypeSpecSimple INT =
            new TypeSpecSimple("Int", "java.lang.Integer", IDX_INT, "cast(? as int)");
    public static TypeSpecSimple BIGINT =
            new TypeSpecSimple("Bigint", "java.lang.Long", IDX_BIGINT, "cast(? as bigint)");
    public static TypeSpecSimple FLOAT =
            new TypeSpecSimple("Float", "java.lang.Float", IDX_FLOAT, "cast(? as float)");
    public static TypeSpecSimple DOUBLE =
            new TypeSpecSimple("Double", "java.lang.Double", IDX_DOUBLE, "cast(? as double)");
    public static TypeSpecSimple DATE =
            new TypeSpecSimple("Date", "java.sql.Date", IDX_DATE, "cast(? as date)");
    public static TypeSpecSimple TIME =
            new TypeSpecSimple("Time", "java.sql.Time", IDX_TIME, "cast(? as time)");
    public static TypeSpecSimple TIMESTAMP =
            new TypeSpecSimple(
                    "Timestamp", "java.sql.Timestamp", IDX_TIMESTAMP, "cast(? as timestamp)");
    public static TypeSpecSimple DATETIME =
            new TypeSpecSimple(
                    "Datetime", "java.sql.Timestamp", IDX_DATETIME, "cast(? as datetime)");
    public static TypeSpecSimple SYS_REFCURSOR =
            new TypeSpecSimple(
                    "Sys_refcursor",
                    "com.cubrid.plcsql.predefined.sp.SpLib.Query",
                    IDX_SYS_REFCURSOR,
                    null);

    public boolean isNumber() {
        return simpleTypeIdx == IDX_SHORT
                || simpleTypeIdx == IDX_INT
                || simpleTypeIdx == IDX_BIGINT
                || simpleTypeIdx == IDX_NUMERIC
                || simpleTypeIdx == IDX_FLOAT
                || simpleTypeIdx == IDX_DOUBLE;
    }

    public boolean isString() {
        return simpleTypeIdx == IDX_STRING;
    }

    public boolean isDateTime() {
        return simpleTypeIdx == IDX_DATE
                || simpleTypeIdx == IDX_TIME
                || simpleTypeIdx == IDX_DATETIME
                || simpleTypeIdx == IDX_TIMESTAMP;
    }

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    protected TypeSpecSimple(
            String plcName, String fullJavaType, int simpleTypeIdx, String typicalValueStr) {
        super(plcName, getJavaCode(fullJavaType), simpleTypeIdx, typicalValueStr);
        this.fullJavaType = fullJavaType;
    }

    private static String getJavaCode(String fullJavaType) {

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

    private static final Map<String, TypeSpecSimple> javaNameToSpec = new HashMap<>();

    private static void register(TypeSpecSimple spec) {
        javaNameToSpec.put(spec.fullJavaType, spec);
    }

    static {
        register(OBJECT);
        register(BOOLEAN);
        register(STRING);
        register(NUMERIC_ANY);
        register(SHORT);
        register(INT);
        register(BIGINT);
        register(FLOAT);
        register(DOUBLE);
        register(DATE);
        register(TIME);

        // register(TIMESTAMP);   // has the same java type as DATETIME
        javaNameToSpec.put("java.time.ZonedDateTime", TIMESTAMP); // kind of a trick

        register(DATETIME);
        register(SYS_REFCURSOR);
    }
}
