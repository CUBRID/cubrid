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

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitTypeSpecSimple(this);
    }

    public final String name;
    public final String fullJavaType;
    public final String nameOfGetMethod;

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
    public static TypeSpecSimple NULL = new TypeSpecSimple("Null", "Null", null);
    public static TypeSpecSimple CURSOR = new TypeSpecSimple("Cursor", "Cursor", null);

    // (1) used as an argument type of some operators in SpLib
    // (2) used as an expression type when a specific Java type cannot be given
    public static TypeSpecSimple OBJECT = new TypeSpecSimple("Object", "java.lang.Object", "getObject");

    public static TypeSpecSimple BOOLEAN = new TypeSpecSimple("Boolean", "java.lang.Boolean", "getBoolean");
    public static TypeSpecSimple STRING = new TypeSpecSimple("String", "java.lang.String", "getString");
    public static TypeSpecSimple NUMERIC =
            new TypeSpecSimple("Numeric", "java.math.BigDecimal", "getBigDecimal");
    public static TypeSpecSimple SHORT = new TypeSpecSimple("Short", "java.lang.Short", "getShort");
    public static TypeSpecSimple INT = new TypeSpecSimple("Int", "java.lang.Integer", "getInt");
    public static TypeSpecSimple BIGINT = new TypeSpecSimple("Bigint", "java.lang.Long", "getLong");
    public static TypeSpecSimple FLOAT = new TypeSpecSimple("Float", "java.lang.Float", "getFloat");
    public static TypeSpecSimple DOUBLE = new TypeSpecSimple("Double", "java.lang.Double", "getDouble");
    public static TypeSpecSimple DATE = new TypeSpecSimple("Date", "java.sql.Date", "getDate");
    public static TypeSpecSimple TIME = new TypeSpecSimple("Time", "java.sql.Time", "getTime");
    public static TypeSpecSimple TIMESTAMP =
            new TypeSpecSimple("Timestamp", "java.sql.Timestamp", "getTimestamp");
    public static TypeSpecSimple DATETIME =
            new TypeSpecSimple("Datetime", "java.sql.Timestamp", "getTimestamp");
    public static TypeSpecSimple SYS_REFCURSOR =
            new TypeSpecSimple("Sys_refcursor", "com.cubrid.plcsql.predefined.sp.SpLib.Query", null);

    /* TODO: restore later
    public static TypeSpecSimple SET = of("java.util.Set");
    public static TypeSpecSimple MULTISET = of("org.apache.commons.collections4.MultiSet");
    public static TypeSpecSimple LIST = of("java.util.List");
     */

    // ------------------------------------------------------------------
    // Private
    // ------------------------------------------------------------------

    private TypeSpecSimple(String name, String fullJavaType, String nameOfGetMethod) {
        super(getJavaCode(fullJavaType));
        this.name = name;
        this.fullJavaType = fullJavaType;
        this.nameOfGetMethod = nameOfGetMethod;
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
        register(NUMERIC);
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
