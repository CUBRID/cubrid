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
import com.cubrid.plcsql.compiler.ast.TypeSpecNumeric;
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

    public abstract String javaCode(String exprJavaCode);

    // for dummies in the CoercionStore
    protected Coercion() {
        src = dst = null;
    };

    protected Coercion(TypeSpecSimple src, TypeSpecSimple dst) {
        this.src = src;
        this.dst = dst;
    }

    public Coercion getReversion() {
        // getReversion() is only used for code generation of argument passing to OUT parameters.
        // and the src and dst types are those written by the users in the program
        assert TypeSpecSimple.isUserType(src);
        assert TypeSpecSimple.isUserType(dst);

        return getCoercion(dst, src);
    }

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
            return Identity.getInstance(src0);
        } else if (src0.equals(TypeSpecSimple.NULL)) {
            // cast NULL: in order for Javac dst pick the right version among operator function
            // overloads when all the arguments are nulls
            return Cast.getInstance(src0, dst0);
        } else if (dst0.equals(TypeSpecSimple.OBJECT)) {
            return Identity.getInstance(src0, dst0);
        }

        Coercion ret = Conversion.getInstance(src0, dst0);
        if (ret == null && src0.simpleTypeIdx == dst0.simpleTypeIdx) {
            if (src0.simpleTypeIdx == TypeSpecSimple.IDX_NUMERIC) {
                ret = Identity.getInstance(src0, dst0);
            } else {
                assert false;
            }
        }

        if (ret != null && dst0 instanceof TypeSpecNumeric) {
            // when dst0 is a NUMERIC type with specific precision and scale
            TypeSpecNumeric tsNumeric = (TypeSpecNumeric) dst0;
            ret = new CoerceAndCheckPrecision(ret, tsNumeric.precision, tsNumeric.scale);
        }

        return ret;
    }

    // ----------------------------------------------
    // coercion cases
    // ----------------------------------------------

    public static class CoerceAndCheckPrecision extends Coercion {

        public Coercion c;
        public int prec;
        public short scale;

        public CoerceAndCheckPrecision(Coercion c, int prec, short scale) {
            super(c.src, c.dst);
            this.c = c;
            this.prec = prec;
            this.scale = scale;
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format(
                    "checkPrecision(%d, (short) %d, %s)", prec, scale, c.javaCode(exprJavaCode));
        }

        @Override
        Coercion create(TypeSpecSimple src, TypeSpecSimple dst) {
            assert false; // CoerceAndCheckPrecision is not memoized in CoercionStore
            return null;
        }
    }

    public static class Identity extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }

        @Override
        Identity create(TypeSpecSimple src, TypeSpecSimple dst) {
            return new Identity(src, dst);
        }

        public static Identity getInstance(TypeSpecSimple ty) {
            return (Identity) memoized.get(ty, ty);
        }

        public static Identity getInstance(TypeSpecSimple src, TypeSpecSimple dst) {
            return (Identity) memoized.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static CoercionStore memoized = new CoercionStore(new Identity());

        private Identity(TypeSpecSimple ty) {
            super(ty, ty);
        }

        protected Identity() {}; // for dummy in the CoercionStore

        private Identity(TypeSpecSimple src, TypeSpecSimple dst) {
            super(src, dst);
        }
    }

    public static class Cast extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", dst.javaCode(), exprJavaCode);
        }

        @Override
        Cast create(TypeSpecSimple src, TypeSpecSimple dst) {
            assert false; // Cast is not memoized in CoercionStore
            return null;
        }

        public static Cast getInstance(TypeSpecSimple src, TypeSpecSimple dst) {
            assert src == TypeSpecSimple.NULL;
            return instances.get(dst.simpleTypeIdx);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static Map<Integer, Cast> instances = new HashMap<>();

        static {
            // NOTE: there is no Cast coercion dst NULL
            instances.put(
                    TypeSpecSimple.IDX_OBJECT,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.OBJECT));
            instances.put(
                    TypeSpecSimple.IDX_BOOLEAN,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.BOOLEAN));
            instances.put(
                    TypeSpecSimple.IDX_STRING,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.STRING));
            instances.put(
                    TypeSpecSimple.IDX_SHORT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.SHORT));
            instances.put(
                    TypeSpecSimple.IDX_INT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.INT));
            instances.put(
                    TypeSpecSimple.IDX_BIGINT,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.BIGINT));
            instances.put(
                    TypeSpecSimple.IDX_NUMERIC,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.NUMERIC_ANY));
            instances.put(
                    TypeSpecSimple.IDX_FLOAT, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.FLOAT));
            instances.put(
                    TypeSpecSimple.IDX_DOUBLE,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DOUBLE));
            instances.put(
                    TypeSpecSimple.IDX_DATE, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DATE));
            instances.put(
                    TypeSpecSimple.IDX_TIME, new Cast(TypeSpecSimple.NULL, TypeSpecSimple.TIME));
            instances.put(
                    TypeSpecSimple.IDX_DATETIME,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.DATETIME));
            instances.put(
                    TypeSpecSimple.IDX_TIMESTAMP,
                    new Cast(TypeSpecSimple.NULL, TypeSpecSimple.TIMESTAMP));
            instances.put(
                    TypeSpecSimple.IDX_SYS_REFCURSOR,
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
        Conversion create(TypeSpecSimple src, TypeSpecSimple dst) {
            return new Conversion(src, dst);
        }

        public static Conversion getInstance(TypeSpecSimple src, TypeSpecSimple dst) {

            Set<Integer> possibleTargets = possibleCasts.get(src.simpleTypeIdx);
            if (possibleTargets == null || !possibleTargets.contains(dst.simpleTypeIdx)) {
                return null;
            }

            return (Conversion) memoized.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static final CoercionStore memoized = new CoercionStore(new Conversion());

        protected Conversion() {}; // for dummy in the CoercionStore

        private Conversion(TypeSpecSimple src, TypeSpecSimple dst) {
            super(src, dst);
            assert !dst.equals(TypeSpecSimple.NULL); // dst cannot be NULL type
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    abstract Coercion create(TypeSpecSimple src, TypeSpecSimple dst); // used inside CoercionStore

    private static class CoercionStore {

        CoercionStore(Coercion dummy) {
            this.dummy = dummy;
        }

        synchronized Coercion get(TypeSpecSimple src, TypeSpecSimple dst) {

            Map<TypeSpecSimple, Coercion> storeInner = store.get(src);
            if (storeInner == null) {
                storeInner = new HashMap<>();
                store.put(src, storeInner);
            }

            Coercion c = storeInner.get(dst);
            if (c == null) {
                c = dummy.create(src, dst);
                storeInner.put(dst, c);
            }

            return c;
        }

        // ---------------------------------------
        // Private
        // ---------------------------------------

        private final Map<TypeSpecSimple, Map<TypeSpecSimple, Coercion>> store = new HashMap<>();
        private final Coercion dummy;
    }

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
                TypeSpecSimple.IDX_TIME, new HashSet(Arrays.asList(TypeSpecSimple.IDX_STRING)));
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
