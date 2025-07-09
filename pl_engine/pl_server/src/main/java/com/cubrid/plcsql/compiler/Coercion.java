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
import com.cubrid.plcsql.compiler.type.TypeNumeric;
import com.cubrid.plcsql.compiler.type.TypeRecord;
import com.cubrid.plcsql.compiler.type.TypeVarchar;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;

public abstract class Coercion {

    public final Type src;
    public final Type dst;

    public abstract String javaCode(String exprJavaCode);

    protected Coercion(Type src, Type dst) {
        this.src = src;
        this.dst = dst;
    }

    public Coercion create(Type src, Type dst) {
        throw new RuntimeException(
                "unreachable"); // overridden only by subclasses that are stoed in CoercionStore
    }

    public Coercion getReversion(InstanceStore iStore) {
        // getReversion() is only used for code generation of argument passing to OUT parameters.
        // and the src and dst types are those written by the users in the program
        assert Type.isUserType(src);
        assert Type.isUserType(dst);

        return getCoercion(iStore, dst, src);
    }

    public static Coercion getCoercion(InstanceStore iStore, Type src, Type dst) {

        if (dst instanceof TypeRecord) {
            if (src == Type.NULL) {
                return new NullToRecord(src, dst);
            } else if (src instanceof TypeRecord) {

                if (src == dst) {
                    return Identity.getInstance(iStore, src);
                }

                TypeRecord srcRec = (TypeRecord) src;
                TypeRecord dstRec = (TypeRecord) dst;

                // conditions of compatibility of two different record types
                // 1. number of fields must be equal
                // 2. corresponding fields must be assign comapatible

                if (srcRec.selectList.size() != dstRec.selectList.size()) {
                    return null; // the numbers of fields do not match
                }

                int len = srcRec.selectList.size();
                Coercion[] fieldCoercions = new Coercion[len];

                for (int i = 0; i < len; i++) {
                    Misc.Pair<String, Type> srcField = srcRec.selectList.get(i);
                    Misc.Pair<String, Type> dstField = dstRec.selectList.get(i);

                    Coercion c = getCoercion(iStore, srcField.e2, dstField.e2);
                    if (c == null) {
                        return null; // coercion is not available for this field
                    } else {
                        fieldCoercions[i] = c;
                    }
                }

                return RecordToRecord.getInstance(iStore, srcRec, dstRec, fieldCoercions);
            }

            return null;
        }

        if (src == dst) {
            return Identity.getInstance(iStore, src);
        } else if (src == Type.NULL) {
            // cast NULL: in order for Javac dst pick the right version among operator function
            // overloads when all the arguments are nulls
            return Cast.getStaticInstance(src, dst);
        } else if (dst == Type.OBJECT) {
            return Identity.getInstance(iStore, src, dst);
        }

        Coercion ret = Conversion.getInstance(iStore, src, dst);
        if (ret == null && src.idx == dst.idx) {
            if (src.idx == Type.IDX_NUMERIC || src.idx == Type.IDX_STRING) {
                ret = Identity.getInstance(iStore, src, dst);
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

    public static class NullToRecord extends Coercion {

        public NullToRecord(Type src, Type dst) {
            super(src, dst);
        }

        @Override
        public String javaCode(String exprJavaCode) {
            // a new instance of the target record with Null fields
            return String.format("new %s().setNull(%s)", dst.javaCode, exprJavaCode);
        }
    }

    public static class RecordToRecord extends Coercion {

        public static final RecordToRecord DUMMY = new RecordToRecord(null, null);

        Coercion[] fieldCoercions;

        @Override
        public String javaCode(String exprJavaCode) {
            assert false; // maybe, unreachable
            return String.format(
                    "setFieldsOf%1$s_To_%2$s(%3$s, new %2$s())",
                    src.javaCode, dst.javaCode, exprJavaCode);
        }

        @Override
        public Coercion create(Type src, Type dst) {
            return new RecordToRecord(src, dst);
        }

        public static RecordToRecord getInstance(
                InstanceStore iStore, Type src, Type dst, Coercion[] fieldCoercions) {
            RecordToRecord ret = (RecordToRecord) iStore.recToRec.get(src, dst);
            if (ret.fieldCoercions == null) {
                ret.fieldCoercions = fieldCoercions;
            }
            return ret;
        }

        public static List<String> getAllJavaCode(InstanceStore iStore) {

            List<String> lines = new LinkedList<>();

            for (Map<Type, Coercion> inner : iStore.recToRec.store.values()) {
                for (Coercion c : inner.values()) {
                    RecordToRecord rtr = (RecordToRecord) c;
                    lines.addAll(rtr.getCoercionFuncCode());
                }
            }

            return lines;
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private RecordToRecord(Type src, Type dst) {
            super(src, dst);
        }

        private List<String> getCoercionFuncCode() {

            List<String> lines = new LinkedList<>();

            lines.add(
                    String.format(
                            "private static %2$s setFieldsOf%1$s_To_%2$s(%1$s src, %2$s dst) {",
                            src.javaCode, dst.javaCode));
            lines.add("  if (src == null) {");
            lines.add("    return dst.setNull(null);");
            lines.add("  }");
            lines.add("  return dst.set(");

            TypeRecord srcRec = (TypeRecord) src;
            assert srcRec.selectList.size() == fieldCoercions.length;

            int i = 0;
            for (Misc.Pair<String, Type> field : srcRec.selectList) {
                lines.add(
                        "    "
                                + (i == 0 ? "" : ",")
                                + fieldCoercions[i].javaCode("src." + field.e1 + "[0]"));
                i++;
            }

            lines.add("  );");
            lines.add("}");

            return lines;
        }
    }

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
    }

    public static class Identity extends Coercion {

        public static final Identity DUMMY = new Identity(null, null);

        @Override
        public String javaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }

        @Override
        public Identity create(Type src, Type dst) {
            return new Identity(src, dst);
        }

        public static Identity getInstance(InstanceStore iStore, Type ty) {
            return (Identity) iStore.identity.get(ty, ty);
        }

        public static Identity getInstance(InstanceStore iStore, Type src, Type dst) {
            return (Identity) iStore.identity.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private Identity(Type ty) {
            super(ty, ty);
        }

        private Identity(Type src, Type dst) {
            super(src, dst);
        }
    }

    public static class Cast extends Coercion {

        @Override
        public String javaCode(String exprJavaCode) {
            return String.format("(%s) %s", dst.javaCode, exprJavaCode);
        }

        public static Cast getStaticInstance(Type src, Type dst) {
            assert src == Type.NULL;
            return instances.get(dst.idx);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        // NOTE: never changing after the initilization during the Cast class initialization
        private static Map<Integer, Cast> instances = new HashMap<>();

        static {
            // NOTE: there is no Cast coercion dst NULL
            instances.put(Type.IDX_OBJECT, new Cast(Type.NULL, Type.OBJECT));
            instances.put(Type.IDX_BOOLEAN, new Cast(Type.NULL, Type.BOOLEAN));
            instances.put(Type.IDX_STRING, new Cast(Type.NULL, Type.STRING_ANY));
            instances.put(Type.IDX_SHORT, new Cast(Type.NULL, Type.SHORT));
            instances.put(Type.IDX_INT, new Cast(Type.NULL, Type.INT));
            instances.put(Type.IDX_BIGINT, new Cast(Type.NULL, Type.BIGINT));
            instances.put(Type.IDX_NUMERIC, new Cast(Type.NULL, Type.NUMERIC_ANY));
            instances.put(Type.IDX_FLOAT, new Cast(Type.NULL, Type.FLOAT));
            instances.put(Type.IDX_DOUBLE, new Cast(Type.NULL, Type.DOUBLE));
            instances.put(Type.IDX_DATE, new Cast(Type.NULL, Type.DATE));
            instances.put(Type.IDX_TIME, new Cast(Type.NULL, Type.TIME));
            instances.put(Type.IDX_DATETIME, new Cast(Type.NULL, Type.DATETIME));
            instances.put(Type.IDX_TIMESTAMP, new Cast(Type.NULL, Type.TIMESTAMP));
            instances.put(Type.IDX_SYS_REFCURSOR, new Cast(Type.NULL, Type.SYS_REFCURSOR));
        }

        private Cast(Type src, Type dst) {
            super(src, dst);
            assert src == Type.NULL; // only NULL type is possible for the src type
            assert dst != Type.NULL; // dst cannot be NULL type
        }
    }

    public static class Conversion extends Coercion {

        public static final Conversion DUMMY = new Conversion(null, null);

        @Override
        public String javaCode(String exprJavaCode) {
            String srcName = Type.getTypeByIdx(src.idx).plcName;
            String dstName = Type.getTypeByIdx(dst.idx).plcName;
            return String.format("conv%sTo%s(%s)", srcName, dstName, exprJavaCode);
        }

        @Override
        public Conversion create(Type src, Type dst) {
            return new Conversion(src, dst);
        }

        public static Conversion getInstance(InstanceStore iStore, Type src, Type dst) {

            assert dst != Type.NULL; // dst cannot be NULL type

            Set<Integer> possibleTargets = possibleCasts.get(src.idx);
            if (possibleTargets == null || !possibleTargets.contains(dst.idx)) {
                return null;
            }

            return (Conversion) iStore.conv.get(src, dst);
        }

        // ----------------------------------------------
        // Private
        // ----------------------------------------------

        private Conversion(Type src, Type dst) {
            super(src, dst);
        }
    }

    // ----------------------------------------------
    // Private
    // ----------------------------------------------

    // NOTE: never changing after the initilization during the Coercion class initialization
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
                new HashSet(Arrays.asList(Type.IDX_DATETIME, Type.IDX_TIMESTAMP, Type.IDX_STRING)));
        possibleCasts.put(Type.IDX_TIME, new HashSet(Arrays.asList(Type.IDX_STRING)));
        possibleCasts.put(
                Type.IDX_TIMESTAMP,
                new HashSet(
                        Arrays.asList(
                                Type.IDX_DATETIME, Type.IDX_DATE, Type.IDX_TIME, Type.IDX_STRING)));
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
