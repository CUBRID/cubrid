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

import com.cubrid.plcsql.compiler.ast.TypeSpec;
import com.cubrid.plcsql.compiler.ast.TypeSpecPercent;
import com.cubrid.plcsql.compiler.ast.TypeSpecSimple;
import com.cubrid.plcsql.compiler.ast.TypeSpecNumeric;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public abstract class Coercion {

    public abstract String javaCode(String exprJavaCode);

    public static Coercion getCoercion(TypeSpec from, TypeSpec to) {

        if (from instanceof TypeSpecPercent) {
            from = ((TypeSpecPercent) from).resolvedType;
            assert from != null;
        }
        if (to instanceof TypeSpecPercent) {
            to = ((TypeSpecPercent) to).resolvedType;
            assert to != null;
        }

        if (from == to) {
            return IDENTITY;
        } else if (from == TypeSpecSimple.NULL) {
            // why NULL?: in order for Javac to pick the right version among operator function
            // overloads when all the arguments are nulls
            return new Cast(to);
        } else if (to == TypeSpecSimple.OBJECT) {
            return IDENTITY;
        }

        Coercion ret = null;

        Set<Integer> possibleTargets = possibleCasts.get(from.simpleTypeIdx);
        if (possibleTargets != null && possibleTargets.contains(to.simpleTypeIdx)) {
            ret = new Conversion(from.plcName, to.plcName);
        } else if (from.simpleTypeIdx == to.simpleTypeIdx && from.simpleTypeIdx == TypeSpecSimple.IDX_NUMERIC) {
            ret = IDENTITY;    // TODO: do more
        }

        if (ret != null && to instanceof TypeSpecNumeric) {
            // when 'to' is a NUMERIC type with specific precision and scale
            TypeSpecNumeric tsNumeric = (TypeSpecNumeric) to;
            ret = new CoerceAndCheckPrecision(ret, tsNumeric.precision, tsNumeric.scale);
        }

        return ret;
    }

    // ----------------------------------------------
    // cases
    // ----------------------------------------------

    public static class CoerceAndCheckPrecision extends Coercion {

        public Coercion c;
        public int prec;
        public short scale;

        public CoerceAndCheckPrecision(Coercion c, int prec, short scale) {
            this.c = c;
            this.prec = prec;
            this.scale = scale;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("checkPrecision(%d, %d, %s)", prec, scale, c.javaCode(exprJavaCode));
        }
    }

    public static class Identity extends Coercion {
        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }
    }

    public static Coercion IDENTITY = new Identity();

    public static class Cast extends Coercion {
        public TypeSpec to;

        public Cast(TypeSpec to) {
            this.to = to;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", to.javaCode(), exprJavaCode);
        }
    }

    public static class Conversion extends Coercion {
        public String from;
        public String to;

        public Conversion(String from, String to) {
            this.from = from;
            this.to = to;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("conv%sTo%s(%s)", from, to, exprJavaCode);
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    private static final Map<Integer, Set<Integer>> possibleCasts = new HashMap<>();

    static {
        possibleCasts.put(
                TypeSpecSimple.IDX_DATETIME,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_DATE,
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_STRING)));
        possibleCasts.put(
                TypeSpecSimple.IDX_DATE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_DATETIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_STRING)));
        possibleCasts.put(
                TypeSpecSimple.IDX_TIME,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_STRING)));
        possibleCasts.put(
                TypeSpecSimple.IDX_TIMESTAMP,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_DATETIME,
                                TypeSpecSimple.IDX_DATE,
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_STRING)));
        possibleCasts.put(
                TypeSpecSimple.IDX_DOUBLE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_FLOAT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_NUMERIC,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_BIGINT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC)));
        possibleCasts.put(
                TypeSpecSimple.IDX_INT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_SHORT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_STRING,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_DATETIME,
                                TypeSpecSimple.IDX_DATE,
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.IDX_OBJECT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.IDX_DATETIME,
                                TypeSpecSimple.IDX_DATE,
                                TypeSpecSimple.IDX_TIME,
                                TypeSpecSimple.IDX_TIMESTAMP,
                                TypeSpecSimple.IDX_INT,
                                TypeSpecSimple.IDX_SHORT,
                                TypeSpecSimple.IDX_STRING,
                                TypeSpecSimple.IDX_DOUBLE,
                                TypeSpecSimple.IDX_FLOAT,
                                TypeSpecSimple.IDX_NUMERIC,
                                TypeSpecSimple.IDX_BIGINT)));
    }
}
