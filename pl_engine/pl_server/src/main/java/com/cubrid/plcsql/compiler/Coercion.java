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

import com.cubrid.plcsql.compiler.type.Type;
import com.cubrid.plcsql.compiler.type.TypeChar;
import com.cubrid.plcsql.compiler.type.TypeVarchar;
import com.cubrid.plcsql.compiler.type.TypeNumeric;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public abstract class Coercion {

    public final Type src;
    public final Type dst;

    public abstract String javaCode(String exprJavaCode);

    // for dummies in the CoercionStore
    protected Coercion() {
        src = dst = null;
    };

    protected Coercion(Type src, Type dst) {
        this.src = src;
        this.dst = dst;
    }

    public Coercion getReversion() {
        // getReversion() is only used for code generation of argument passing to OUT parameters.
        // and the src and dst types are those written by the users in the program
        assert Type.isUserType(src);
        assert Type.isUserType(dst);

        return getCoercion(dst, src);
    }

    public static Coercion getCoercion(Type src, Type dst) {

        if (src == dst) {
            return Identity.getInstance(src);
        } else if (src == Type.NULL) {
            // cast NULL: in order for Javac dst pick the right version among operator function
            // overloads when all the arguments are nulls
            return Cast.getInstance(src, dst);
        } else if (dst == Type.OBJECT) {
            return Identity.getInstance(src, dst);
        }

        Coercion ret = Conversion.getInstance(src, dst);
        if (ret == null && src.idx == dst.idx) {
            if (src.idx == Type.IDX_NUMERIC || src.idx == Type.IDX_STRING) {
                ret = Identity.getInstance(src, dst);
            } else {
                assert false;
            }
        }

        if (ret != null) {
            if (dst instanceof TypeNumeric) {
                // when 'dst' is a NUMERIC type with specific precision and scale
                TypeNumeric tyNumeric = (TypeNumeric) dst;
                ret = new CoerceAndCheckPrecision(ret, tyNumeric.precision, tyNumeric.scale);
            } else if (dst instanceof TypeChar) {
                // when 'dst' is a CHAR type with a specific length
                TypeChar tyChar = (TypeChar) dst;
                ret = new CoerceAndCheckStrLength(ret, tyChar.length, true);
            } else if (dst instanceof TypeVarchar) {
                // when 'dst' is a VARCHAR type with a specific length
                TypeVarchar tyVarchar = (TypeVarchar) dst;
                ret = new CoerceAndCheckStrLength(ret, tyVarchar.length, false);
            }
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
        Coercion create(Type src, Type dst) {
            assert false; // CoerceAndCheckPrecision is not memoized in CoercionStore
            return null;
        }
    }

    public static class CoerceAndCheckStrLength extends Coercion {

        public Coercion c;
        public int length;
        public boolean isChar;

        public CoerceAndCheckStrLength(Coercion c, int length, boolean isChar) {
            super(c.src, c.dst);
            this.c = c;
            this.length = length;
            this.isChar = isChar; // true for Char, and false for Varchar
        }

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format(
                    "checkStrLength(%s, %d, %s)", isChar, length, c.javaCode(exprJavaCode));
        }

        @Override
        Coercion create(Type src, Type dst) {
            assert false; // CoerceAndCheckStrLength is not memoized in CoercionStore
            return null;
        }
    }

    public static class Identity extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }

        @Override
        Identity create(Type src, Type dst) {
            return new Identity(src, dst);
        }

        public static Identity getInstance(Type ty) {
            return (Identity) memoized.get(ty, ty);
        }

        public static Identity getInstance(Type src, Type dst) {
            return (Identity) memoized.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static CoercionStore memoized = new CoercionStore(new Identity());

        private Identity(Type ty) {
            super(ty, ty);
        }

        protected Identity() {}; // for dummy in the CoercionStore

        private Identity(Type src, Type dst) {
            super(src, dst);
        }
    }

    public static class Cast extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", dst.javaCode, exprJavaCode);
        }

        @Override
        Cast create(Type src, Type dst) {
            assert false; // Cast is not memoized in CoercionStore
            return null;
        }

        public static Cast getInstance(Type src, Type dst) {
            assert src == Type.NULL;
            return instances.get(dst.idx);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static Map<Integer, Cast> instances = new HashMap<>();

        static {
            // NOTE: there is no Cast coercion dst NULL
            instances.put(
                    Type.IDX_OBJECT, new Cast(Type.NULL, Type.OBJECT));
            instances.put(
                    Type.IDX_BOOLEAN, new Cast(Type.NULL, Type.BOOLEAN));
            instances.put(
                    Type.IDX_STRING, new Cast(Type.NULL, Type.STRING_ANY));
            instances.put(
                    Type.IDX_SHORT, new Cast(Type.NULL, Type.SHORT));
            instances.put(
                    Type.IDX_INT, new Cast(Type.NULL, Type.INT));
            instances.put(
                    Type.IDX_BIGINT, new Cast(Type.NULL, Type.BIGINT));
            instances.put(
                    Type.IDX_NUMERIC, new Cast(Type.NULL, Type.NUMERIC_ANY));
            instances.put(
                    Type.IDX_FLOAT, new Cast(Type.NULL, Type.FLOAT));
            instances.put(
                    Type.IDX_DOUBLE, new Cast(Type.NULL, Type.DOUBLE));
            instances.put(
                    Type.IDX_DATE, new Cast(Type.NULL, Type.DATE));
            instances.put(
                    Type.IDX_TIME, new Cast(Type.NULL, Type.TIME));
            instances.put(
                    Type.IDX_DATETIME, new Cast(Type.NULL, Type.DATETIME));
            instances.put(
                    Type.IDX_TIMESTAMP, new Cast(Type.NULL, Type.TIMESTAMP));
            instances.put(
                    Type.IDX_SYS_REFCURSOR, new Cast(Type.NULL, Type.SYS_REFCURSOR));
        }

        private Cast(Type src, Type dst) {
            super(src, dst);
            assert src == Type.NULL; // only NULL type is possible for the src type
            assert dst != Type.NULL; // dst cannot be NULL type
        }
    }

    public static class Conversion extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            String srcName = Type.getTypeByIdx(src.idx).plcName;
            String dstName = Type.getTypeByIdx(dst.idx).plcName;
            return String.format("conv%sTo%s(%s)", srcName, dstName, exprJavaCode);
        }

        @Override
        Conversion create(Type src, Type dst) {
            return new Conversion(src, dst);
        }

        public static Conversion getInstance(Type src, Type dst) {

            Set<Integer> possibleTargets = possibleCasts.get(src.idx);
            if (possibleTargets == null || !possibleTargets.contains(dst.idx)) {
                return null;
            }

            return (Conversion) memoized.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private static final CoercionStore memoized = new CoercionStore(new Conversion());

        protected Conversion() {}; // for dummy in the CoercionStore

        private Conversion(Type src, Type dst) {
            super(src, dst);
            assert dst != Type.NULL; // dst cannot be NULL type
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    abstract Coercion create(Type src, Type dst); // used inside CoercionStore

    private static class CoercionStore {

        CoercionStore(Coercion dummy) {
            this.dummy = dummy;
        }

        synchronized Coercion get(Type src, Type dst) {

            Map<Type, Coercion> storeInner = store.get(src);
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

        private final Map<Type, Map<Type, Coercion>> store = new HashMap<>();
        private final Coercion dummy;
    }

    private static final Map<Integer, Set<Integer>> possibleCasts = new HashMap<>();

    static {
        possibleCasts.put(
                Type.IDX_DATETIME,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATE,
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_STRING)));
        possibleCasts.put(
                Type.IDX_DATE,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATETIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_STRING)));
        possibleCasts.put(
                Type.IDX_TIME, new HashSet(Arrays.asList(Type.IDX_STRING)));
        possibleCasts.put(
                Type.IDX_TIMESTAMP,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATETIME,
                                Type.IDX_DATE,
                                Type.IDX_TIME,
                                Type.IDX_STRING)));
        possibleCasts.put(
                Type.IDX_DOUBLE,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_FLOAT,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_NUMERIC,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_BIGINT,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC)));
        possibleCasts.put(
                Type.IDX_INT,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_SHORT,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_STRING,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATETIME,
                                Type.IDX_DATE,
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
        possibleCasts.put(
                Type.IDX_OBJECT,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATETIME,
                                Type.IDX_DATE,
                                Type.IDX_TIME,
                                Type.IDX_TIMESTAMP,
                                Type.IDX_INT,
                                Type.IDX_SHORT,
                                Type.IDX_STRING,
                                Type.IDX_DOUBLE,
                                Type.IDX_FLOAT,
                                Type.IDX_NUMERIC,
                                Type.IDX_BIGINT)));
    }
}
