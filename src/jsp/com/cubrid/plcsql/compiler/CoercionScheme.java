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
import com.cubrid.plcsql.compiler.ast.TypeSpecSimple;
import java.util.List;
import java.util.ArrayList;

public enum CoercionScheme {
    CompOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            assert argTypes.size() == 2;

            TypeSpec lType = argTypes.get(0);
            int lTypeIdx = lType.simpleTypeIdx;
            assert lTypeIdx >= 0 && lTypeIdx < TypeSpecSimple.COUNT_OF_IDX;
            TypeSpec rType = argTypes.get(1);
            int rTypeIdx = rType.simpleTypeIdx;
            assert rTypeIdx >= 0 && rTypeIdx < TypeSpecSimple.COUNT_OF_IDX;

            TypeSpec commonTy;
            int row, col;
            if (lTypeIdx >= rTypeIdx) {
                row = lTypeIdx;
                col = rTypeIdx;
            } else {
                row = rTypeIdx;
                col = lTypeIdx;
            }
            commonTy = compOpCommonType[row][col];
            if (commonTy == null) {
                return null;    // not applicable to this argument types
            }

            Coerce c;
            c = Coerce.getCoerce(lType, commonTy);
            assert c != null;
            outCoercions.add(c);
            c = Coerce.getCoerce(rType, commonTy);
            assert c != null;
            outCoercions.add(c);

            List<TypeSpec> ret = new ArrayList<>();
            ret.add(commonTy);
            ret.add(commonTy);

            return ret;
        }
    },

    ArithOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {

            if (argTypes.size() == 2) {

                // +, -, *, /

                TypeSpec lType = argTypes.get(0);
                TypeSpec rType = argTypes.get(1);

                int lTypeIdx = lType.simpleTypeIdx;
                assert lTypeIdx >= 0 && lTypeIdx < TypeSpecSimple.COUNT_OF_IDX;
                int rTypeIdx = rType.simpleTypeIdx;
                assert rTypeIdx >= 0 && rTypeIdx < TypeSpecSimple.COUNT_OF_IDX;

                TypeSpec commonTy;
                int row, col;
                if (lTypeIdx >= rTypeIdx) {
                    row = lTypeIdx;
                    col = rTypeIdx;
                } else {
                    row = rTypeIdx;
                    col = lTypeIdx;
                }
                commonTy = arithOpCommonType[row][col];
                if (commonTy == null && opName.equals("opSubtract")) {
                    commonTy = subtractCommonTypeExt[row][col];
                }

                if (commonTy == null) {

                    if (lType.isDateTime()) {
                        if ((opName.equals("opAdd") && (rType.isString() || rType.isNumber())) ||
                            (opName.equals("opSubtract") && rType.isNumber())) {

                            // date/time + string/number, or
                            // date/time - number

                            outCoercions.add(Coerce.IDENTITY);
                            Coerce c = Coerce.getCoerce(rType, TypeSpecSimple.BIGINT);
                            assert c != null;
                            outCoercions.add(c);

                            List<TypeSpec> ret = new ArrayList<>();
                            ret.add(lType);
                            ret.add(TypeSpecSimple.BIGINT);

                            return ret;
                        }
                    }

                    if (rType.isDateTime()) {
                        if (opName.equals("opAdd") && (lType.isString() || lType.isNumber())) {

                            // string/number + date/time

                            Coerce c = Coerce.getCoerce(lType, TypeSpecSimple.BIGINT);
                            assert c != null;
                            outCoercions.add(c);
                            outCoercions.add(Coerce.IDENTITY);

                            List<TypeSpec> ret = new ArrayList<>();
                            ret.add(TypeSpecSimple.BIGINT);
                            ret.add(rType);

                            return ret;
                        }
                    }

                    return null;
                } else {

                    Coerce c;
                    c = Coerce.getCoerce(lType, commonTy);
                    assert c != null;
                    outCoercions.add(c);
                    c = Coerce.getCoerce(rType, commonTy);
                    assert c != null;
                    outCoercions.add(c);

                    List<TypeSpec> ret = new ArrayList<>();
                    ret.add(commonTy);
                    ret.add(commonTy);

                    return ret;
                }

            } else if (argTypes.size() == 1) {

                // unary -

                TypeSpec lType = argTypes.get(0);
                int lTypeIdx = lType.simpleTypeIdx;
                assert lTypeIdx >= 0 && lTypeIdx < TypeSpecSimple.COUNT_OF_IDX;

                TypeSpec targetTy = arithOpCommonType[lTypeIdx][lTypeIdx];
                if (targetTy == null) {
                    return null;
                } else {
                    Coerce c = Coerce.getCoerce(lType, targetTy);
                    assert c != null;
                    outCoercions.add(c);

                    List<TypeSpec> ret = new ArrayList<>();
                    ret.add(targetTy);

                    return ret;
                }
            } else {
                assert false: "unreachable";
                throw new RuntimeException("unreachable");
            }
        }
    },

    IntArithOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {

            if (argTypes.size() == 2) {

                // mod(%), div

                TypeSpec lType = argTypes.get(0);
                TypeSpec rType = argTypes.get(1);

                int lTypeIdx = lType.simpleTypeIdx;
                assert lTypeIdx >= 0 && lTypeIdx < TypeSpecSimple.COUNT_OF_IDX;
                int rTypeIdx = rType.simpleTypeIdx;
                assert rTypeIdx >= 0 && rTypeIdx < TypeSpecSimple.COUNT_OF_IDX;

                TypeSpec commonTy;
                int row, col;
                if (lTypeIdx >= rTypeIdx) {
                    row = lTypeIdx;
                    col = rTypeIdx;
                } else {
                    row = rTypeIdx;
                    col = lTypeIdx;
                }
                commonTy = intArithOpCommonType[row][col];
                if (commonTy == null) {
                    return null;
                } else {

                    Coerce c;
                    c = Coerce.getCoerce(lType, commonTy);
                    assert c != null;
                    outCoercions.add(c);
                    c = Coerce.getCoerce(rType, commonTy);
                    assert c != null;
                    outCoercions.add(c);

                    List<TypeSpec> ret = new ArrayList<>();
                    ret.add(commonTy);
                    ret.add(commonTy);

                    return ret;
                }

            } else if (argTypes.size() == 1) {

                // ~

                TypeSpec lType = argTypes.get(0);
                int lTypeIdx = lType.simpleTypeIdx;
                assert lTypeIdx >= 0 && lTypeIdx < TypeSpecSimple.COUNT_OF_IDX;

                TypeSpec targetTy = arithOpCommonType[lTypeIdx][lTypeIdx];
                if (targetTy == null) {
                    return null;
                } else {
                    Coerce c = Coerce.getCoerce(lType, targetTy);
                    assert c != null;
                    outCoercions.add(c);

                    List<TypeSpec> ret = new ArrayList<>();
                    ret.add(targetTy);

                    return ret;
                }
            } else {
                assert false: "unreachable";
                throw new RuntimeException("unreachable");
            }
        }
    },

    LogicalOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            return null;    // TODO
        }
    },

    StringOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            return null;    // TODO
        }
    },

    BetweenOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            assert argTypes.size() == 3;
            return null;    // TODO
        }
    },

    InOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            return null;    // TODO
        }
    },

    BitOp {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            return null;    // TODO
        }
    },

    Individual {
        public List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName) {
            return null;    // TODO
        }
    };

    public abstract List<TypeSpec> getCoercions(List<Coerce> outCoercions, List<TypeSpec> argTypes, String opName);

    // -----------------------------------------------------------------------
    // Setting for comparison operators

    static final TypeSpecSimple[][] compOpCommonType =
        new TypeSpecSimple[TypeSpecSimple.COUNT_OF_IDX][TypeSpecSimple.COUNT_OF_IDX];
    static {
        compOpCommonType[TypeSpecSimple.IDX_NULL][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        compOpCommonType[TypeSpecSimple.IDX_OBJECT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        compOpCommonType[TypeSpecSimple.IDX_BOOLEAN][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BOOLEAN;
        compOpCommonType[TypeSpecSimple.IDX_BOOLEAN][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BOOLEAN;
        compOpCommonType[TypeSpecSimple.IDX_BOOLEAN][TypeSpecSimple.IDX_BOOLEAN] = TypeSpecSimple.BOOLEAN;

        compOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.STRING;
        compOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.STRING;
        compOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.STRING;

        compOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.SHORT;
        compOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.SHORT;
        compOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.SHORT;

        compOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.INT;
        compOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.INT;
        compOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.INT;
        compOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.INT;

        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        compOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;

        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_INT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.NUMERIC;

        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.FLOAT;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.FLOAT;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.FLOAT;

        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.DOUBLE;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.DOUBLE;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_INT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.NUMERIC;
        compOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_DOUBLE] = TypeSpecSimple.DOUBLE;

        compOpCommonType[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.DATE;
        compOpCommonType[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.DATE;
        compOpCommonType[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DATE;
        compOpCommonType[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_DATE] = TypeSpecSimple.DATE;

        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_INT] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.TIME;
        compOpCommonType[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_TIME] = TypeSpecSimple.TIME;

        compOpCommonType[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.DATETIME;
        compOpCommonType[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.DATETIME;
        compOpCommonType[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DATETIME;
        compOpCommonType[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_DATETIME] = TypeSpecSimple.DATETIME;

        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_INT] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.TIMESTAMP;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_DATE] = TypeSpecSimple.DATE;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_DATETIME] = TypeSpecSimple.DATETIME;
        compOpCommonType[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_TIMESTAMP] = TypeSpecSimple.TIMESTAMP;
    }

    // -----------------------------------------------------------------------
    // Setting for arithmetic operators (unary -, *, /, +, binary -)

    static final TypeSpecSimple[][] arithOpCommonType =
        new TypeSpecSimple[TypeSpecSimple.COUNT_OF_IDX][TypeSpecSimple.COUNT_OF_IDX];
    static final TypeSpecSimple[][] subtractCommonTypeExt =
        new TypeSpecSimple[TypeSpecSimple.COUNT_OF_IDX][TypeSpecSimple.COUNT_OF_IDX];
    static {
        arithOpCommonType[TypeSpecSimple.IDX_NULL][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        arithOpCommonType[TypeSpecSimple.IDX_OBJECT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        arithOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;

        arithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.SHORT;
        arithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.SHORT;
        arithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.SHORT;

        arithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.INT;
        arithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.INT;
        arithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.INT;
        arithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.INT;

        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        arithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;

        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.NUMERIC;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.NUMERIC;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.NUMERIC;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_INT] = TypeSpecSimple.NUMERIC;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.NUMERIC;
        arithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.NUMERIC;

        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.FLOAT;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.FLOAT;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.FLOAT;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.FLOAT;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.FLOAT;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.FLOAT;

        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_INT] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.DOUBLE;
        arithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_DOUBLE] = TypeSpecSimple.DOUBLE;


        subtractCommonTypeExt[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DATETIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_DATE][TypeSpecSimple.IDX_DATE] = TypeSpecSimple.DATE;

        subtractCommonTypeExt[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.TIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_TIME][TypeSpecSimple.IDX_TIME] = TypeSpecSimple.TIME;

        subtractCommonTypeExt[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DATETIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_DATE] = TypeSpecSimple.DATETIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_DATETIME][TypeSpecSimple.IDX_DATETIME] = TypeSpecSimple.DATETIME;

        subtractCommonTypeExt[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.DATETIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_DATE] = TypeSpecSimple.TIMESTAMP;
        subtractCommonTypeExt[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_DATETIME] = TypeSpecSimple.DATETIME;
        subtractCommonTypeExt[TypeSpecSimple.IDX_TIMESTAMP][TypeSpecSimple.IDX_TIMESTAMP] = TypeSpecSimple.TIMESTAMP;
    }

    // -----------------------------------------------------------------------
    // Setting for integer arithmetic operators (mod, (integer) div, bit compliment)

    static final TypeSpecSimple[][] intArithOpCommonType =
        new TypeSpecSimple[TypeSpecSimple.COUNT_OF_IDX][TypeSpecSimple.COUNT_OF_IDX];
    static {
        intArithOpCommonType[TypeSpecSimple.IDX_NULL][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        intArithOpCommonType[TypeSpecSimple.IDX_OBJECT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.OBJECT;

        intArithOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_STRING][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;

        intArithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.SHORT;
        intArithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.SHORT;
        intArithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_SHORT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.SHORT;

        intArithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.INT;
        intArithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.INT;
        intArithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.INT;
        intArithOpCommonType[TypeSpecSimple.IDX_INT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.INT;

        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_BIGINT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;

        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_NUMERIC][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.BIGINT;

        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_FLOAT][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.BIGINT;

        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NULL] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_OBJECT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_STRING] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_SHORT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_INT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_BIGINT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_NUMERIC] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_FLOAT] = TypeSpecSimple.BIGINT;
        intArithOpCommonType[TypeSpecSimple.IDX_DOUBLE][TypeSpecSimple.IDX_DOUBLE] = TypeSpecSimple.BIGINT;
    }
}
