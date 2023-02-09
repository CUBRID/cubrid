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

import com.cubrid.plcsql.compiler.SemanticError;
import com.cubrid.plcsql.compiler.visitor.AstNodeVisitor;

import com.cubrid.plcsql.compiler.Misc;

import java.util.Map;
import java.util.HashMap;

public class TypeSpecSimple extends TypeSpec {

    @Override
    public <R> R accept(AstNodeVisitor<R> visitor) {
        return visitor.visitTypeSpecSimple(this);
    }

    public final String fullJavaType;

    public static TypeSpecSimple of(String s) {
        TypeSpecSimple ret = singletons.get(s);
        assert ret != null : "wrong java type " + s;
        return ret;
    }

    @Override
    public String toJavaSignature() {
        return fullJavaType;
    }

    private TypeSpecSimple(String fullJavaType) {
        super(getSimpleName(fullJavaType));
        this.fullJavaType = fullJavaType;
    }

    private static String getSimpleName(String fullJavaType) {
        String[] split = fullJavaType.split("\\.");
        assert split.length > 2;
        return split[split.length - 1];
    }

    private static final Map<String, TypeSpecSimple> singletons = new HashMap<>();
    static {
        final String[] javaTypes = new String[] {
            "java.lang.Object",
            "java.lang.Object[]",
            "..Null",       // not an actual java type
            "..Cursor",     // not an actual java type

            "java.lang.Boolean",
            "java.lang.String",
            "java.math.BigDecimal",
            "java.lang.Short",
            "java.lang.Integer",
            "java.lang.Long",
            "java.lang.Float",
            "java.lang.Double",
            "java.time.LocalDate",
            "java.time.LocalTime",
            "java.time.ZonedDateTime",
            "java.time.LocalDateTime",
            "java.util.Set",
            "org.apache.commons.collections4.MultiSet",
            "java.util.List",

            "com.cubrid.plcsql.predefined.sp.SpLib.Query"
        };

        for (String jt: javaTypes) {
            TypeSpecSimple r = singletons.put(jt, new TypeSpecSimple(jt));
            assert r == null;   // no duplicate
        }
    }

    public static TypeSpecSimple OBJECT        = of("java.lang.Object");
    public static TypeSpecSimple NULL          = of("..Null");
    public static TypeSpecSimple CURSOR        = of("..Cursor");

    public static TypeSpecSimple BOOLEAN       = of("java.lang.Boolean");
    public static TypeSpecSimple STRING        = of("java.lang.String");
    public static TypeSpecSimple BIGDECIMAL    = of("java.math.BigDecimal");
    public static TypeSpecSimple SHORT         = of("java.lang.Short");
    public static TypeSpecSimple INTEGER       = of("java.lang.Integer");
    public static TypeSpecSimple LONG          = of("java.lang.Long");
    public static TypeSpecSimple FLOAT         = of("java.lang.Float");
    public static TypeSpecSimple DOUBLE        = of("java.lang.Double");
    public static TypeSpecSimple LOCALDATE     = of("java.time.LocalDate");
    public static TypeSpecSimple LOCALTIME     = of("java.time.LocalTime");
    public static TypeSpecSimple ZONEDDATETIME = of("java.time.ZonedDateTime");
    public static TypeSpecSimple LOCALDATETIME = of("java.time.LocalDateTime");
    public static TypeSpecSimple SET           = of("java.util.Set");
    public static TypeSpecSimple MULTISET      = of("org.apache.commons.collections4.MultiSet");
    public static TypeSpecSimple LIST          = of("java.util.List");

    public static TypeSpecSimple REFCURSOR     = of("com.cubrid.plcsql.predefined.sp.SpLib.Query");
}
