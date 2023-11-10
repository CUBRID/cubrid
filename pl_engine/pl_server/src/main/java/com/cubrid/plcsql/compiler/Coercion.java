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
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public abstract class Coercion {

    public final TypeSpecSimple src;
    public final TypeSpecSimple dst;

    protected Coercion(TypeSpecSimple src, TypeSpecSimple dst) {
        this.src = src;
        this.dst = dst;
    }

    public abstract Coercion getReversion();

    public abstract String javaCode(String exprJavaCode);

    public static Coercion getCoercion(TypeSpec src, TypeSpec dst) {

        if (src instanceof TypeSpecPercent) {
            src = ((TypeSpecPercent) src).resolvedType;
            assert src != null;
        }
        if (dst instanceof TypeSpecPercent) {
            dst = ((TypeSpecPercent) dst).resolvedType;
            assert dst != null;
        }

        assert src instanceof TypeSpecSimple;
        assert dst instanceof TypeSpecSimple;

        TypeSpecSimple src0 = (TypeSpecSimple) src;
        TypeSpecSimple dst0 = (TypeSpecSimple) dst;

        if (src0.equals(dst0)) {
            return Identity.getInstance(dst0);
        } else if (src0.equals(TypeSpecSimple.NULL)) {
            // why cast NULL?: in order for Javac dst pick the right version among operator function
            // overloads when all the arguments are nulls
            return Cast.getInstance(src0, dst0);
        } else if (dst0.equals(TypeSpecSimple.OBJECT)) {
            return Identity.getInstance(dst0);
        }

        return Conversion.getInstance(src0, dst0);
    }

    // ----------------------------------------------
    // coercion cases
    // ----------------------------------------------

    public static class Identity extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }

        @Override
        public Identity getReversion() {
            return this;
        }

        public static Identity getInstance(TypeSpecSimple ty) {
            return instances.get(ty);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static Map<TypeSpecSimple, Identity> instances = new HashMap<>();

        static {
            instances.put(TypeSpecSimple.NULL, new Identity(TypeSpecSimple.NULL));
            instances.put(TypeSpecSimple.OBJECT, new Identity(TypeSpecSimple.OBJECT));
            instances.put(TypeSpecSimple.BOOLEAN, new Identity(TypeSpecSimple.BOOLEAN));
            instances.put(TypeSpecSimple.STRING, new Identity(TypeSpecSimple.STRING));
            instances.put(TypeSpecSimple.SHORT, new Identity(TypeSpecSimple.SHORT));
            instances.put(TypeSpecSimple.INT, new Identity(TypeSpecSimple.INT));
            instances.put(TypeSpecSimple.BIGINT, new Identity(TypeSpecSimple.BIGINT));
            instances.put(TypeSpecSimple.NUMERIC, new Identity(TypeSpecSimple.NUMERIC));
            instances.put(TypeSpecSimple.FLOAT, new Identity(TypeSpecSimple.FLOAT));
            instances.put(TypeSpecSimple.DOUBLE, new Identity(TypeSpecSimple.DOUBLE));
            instances.put(TypeSpecSimple.DATE, new Identity(TypeSpecSimple.DATE));
            instances.put(TypeSpecSimple.TIME, new Identity(TypeSpecSimple.TIME));
            instances.put(TypeSpecSimple.DATETIME, new Identity(TypeSpecSimple.DATETIME));
            instances.put(TypeSpecSimple.TIMESTAMP, new Identity(TypeSpecSimple.TIMESTAMP));
            instances.put(TypeSpecSimple.SYS_REFCURSOR, new Identity(TypeSpecSimple.SYS_REFCURSOR));
        }

        private Identity(TypeSpecSimple ty) {
            super(ty, ty);
        }
    }

    public static class Cast extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", dst.javaCode(), exprJavaCode);
        }

        @Override
        public Cast getReversion() {
            return Cast.getInstance(dst, src);
        }

        public static Cast getInstance(TypeSpecSimple src, TypeSpecSimple dst) {
            return instances.get(dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static Map<TypeSpecSimple, Cast> instances = new HashMap<>();

        static {
            // NOTE: there is no Cast coercion dst NULL
            instances.put(
                    TypeSpecSimple.OBJECT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.OBJECT));
            instances.put(
                    TypeSpecSimple.BOOLEAN, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.BOOLEAN));
            instances.put(
                    TypeSpecSimple.STRING, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.STRING));
            instances.put(
                    TypeSpecSimple.SHORT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.SHORT));
            instances.put(TypeSpecSimple.INT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.INT));
            instances.put(
                    TypeSpecSimple.BIGINT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.BIGINT));
            instances.put(
                    TypeSpecSimple.NUMERIC, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.NUMERIC));
            instances.put(
                    TypeSpecSimple.FLOAT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.FLOAT));
            instances.put(
                    TypeSpecSimple.DOUBLE, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DOUBLE));
            instances.put(TypeSpecSimple.DATE, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DATE));
            instances.put(TypeSpecSimple.TIME, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.TIME));
            instances.put(
                    TypeSpecSimple.DATETIME,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DATETIME));
            instances.put(
                    TypeSpecSimple.TIMESTAMP,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.TIMESTAMP));
            instances.put(
                    TypeSpecSimple.SYS_REFCURSOR,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.SYS_REFCURSOR));
        }

        private Cast(TypeSpecSimple src, TypeSpecSimple dst) {
            super(src, dst);
            assert src.equals(TypeSpecSimple.NULL); // only NULL type is possible for the src type
            assert !dst.equals(TypeSpecSimple.NULL); // dst cannot be NULL type
        }
    }

    public static class Conversion extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("conv%sTo%s(%s)", src.plcName, dst.plcName, exprJavaCode);
        }

        @Override
        public Conversion getReversion() {
            return Conversion.getInstance(dst, src);
        }

        public static Conversion getInstance(TypeSpecSimple src, TypeSpecSimple dst) {

            Set<TypeSpecSimple> possibleTargets = possibleCasts.get(src);
            if (possibleTargets == null || !possibleTargets.contains(dst)) {
                return null;
            }

            Conversion c;
            synchronized (memoized) {
                Map<TypeSpecSimple, Conversion> instances = memoized.get(src);
                if (instances == null) {
                    instances = new HashMap<>();
                    memoized.put(src, instances);
                }

                c = instances.get(dst);
                if (c == null) {
                    c = new Conversion(src, dst);
                    instances.put(dst, c);
                }
            }

            return c;
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static final Map<TypeSpecSimple, Map<TypeSpecSimple, Conversion>> memoized =
                new HashMap<>();

        private Conversion(TypeSpecSimple src, TypeSpecSimple dst) {
            super(src, dst);
            assert !dst.equals(TypeSpecSimple.NULL); // dst cannot be NULL type
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    private static final Map<TypeSpecSimple, Set<TypeSpecSimple>> possibleCasts = new HashMap<>();

    static {
        possibleCasts.put(
                TypeSpecSimple.DATETIME,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.DATE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(TypeSpecSimple.TIME, new HashSet(Arrays.asList(TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.TIMESTAMP,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.STRING)));
        possibleCasts.put(
                TypeSpecSimple.DOUBLE,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.FLOAT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.NUMERIC,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.BIGINT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC)));
        possibleCasts.put(
                TypeSpecSimple.INT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.SHORT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.STRING,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
        possibleCasts.put(
                TypeSpecSimple.OBJECT,
                new HashSet(
                        Arrays.asList(
                                TypeSpecSimple.DATETIME,
                                TypeSpecSimple.DATE,
                                TypeSpecSimple.TIME,
                                TypeSpecSimple.TIMESTAMP,
                                TypeSpecSimple.INT,
                                TypeSpecSimple.SHORT,
                                TypeSpecSimple.STRING,
                                TypeSpecSimple.DOUBLE,
                                TypeSpecSimple.FLOAT,
                                TypeSpecSimple.NUMERIC,
                                TypeSpecSimple.BIGINT)));
    }
}
