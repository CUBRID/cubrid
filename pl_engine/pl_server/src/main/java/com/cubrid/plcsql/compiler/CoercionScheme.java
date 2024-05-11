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
import java.util.ArrayList;
import java.util.List;

public enum CoercionScheme {
    CompOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {
            assert argTypes.size() == 2;

            Type commonTy = getCommonTypeInner(argTypes.get(0), argTypes.get(1), compOpCommonType);
            if (commonTy == null) {
                return null; // not applicable to this argument types
            }

            // The fact that the common type is Null implies that all the arguments are nulls.
            // In this case, we need to change the common type to some non-Null type because
            // Java does not have Null type in its type system.
            // I choose Object as the 'some non-Null type' among possible choices.
            // This will work fine because operators return null when any of their arguemnts are
            // null
            // for all their overloaded versions.
            // (Note that the common type decides which version will be used among the overloaded
            // versions
            //  of operators)
            if (commonTy == Type.NULL) {
                commonTy = Type.OBJECT;
            }

            List<Type> ret = new ArrayList<>();
            for (int i = 0; i < 2; i++) {
                Coercion c = Coercion.getCoercion(argTypes.get(i), commonTy);
                assert c != null;
                outCoercions.add(c);
                ret.add(commonTy);
            }

            return ret;
        }
    },

    NAryCompOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {

            // between, in

            int len = argTypes.size();

            Type headTy = argTypes.get(0);

            Type wholeCommonTy = null;
            for (int i = 1; i < len; i++) {
                Type argType = argTypes.get(i);

                Type commonTy = getCommonTypeInner(headTy, argType, compOpCommonType);
                if (commonTy == null) {
                    return null;
                }

                if (wholeCommonTy == null) {
                    wholeCommonTy = commonTy;
                } else {
                    if (wholeCommonTy == commonTy) {
                        // just keep wholeCommonTy (do nothing)
                    } else {
                        wholeCommonTy = Type.OBJECT; // resort to runtime type check and conversion
                    }
                }
            }

            if (wholeCommonTy == Type.NULL) {
                wholeCommonTy = Type.OBJECT; // see the comment in CompOp
            } else if (wholeCommonTy == Type.OBJECT) {
                // In this case, pairwise common types are not equal to a single type, and
                // pairwise coomparison in opBetween and opIn in SpLib uses runtime type check and
                // conversion, that is, compareWithRuntimeTypeConv().
            }

            List<Type> ret = new ArrayList<>();
            for (int i = 0; i < len; i++) {
                Coercion c = Coercion.getCoercion(argTypes.get(i), wholeCommonTy);
                assert c != null;
                outCoercions.add(c);
                ret.add(wholeCommonTy);
            }

            return ret;
        }
    },

    ArithOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {

            if (argTypes.size() == 2) {

                // +, -, *, /

                Type lType = argTypes.get(0);
                Type rType = argTypes.get(1);

                Type commonTy;
                if (opName.equals("opSubtract")) {
                    commonTy =
                            getCommonTypeInner(
                                    lType, rType, arithOpCommonType, subtractCommonTypeExt);
                } else {
                    commonTy = getCommonTypeInner(lType, rType, arithOpCommonType);
                }

                if (commonTy != null) {

                    if (commonTy == Type.NULL) {
                        commonTy = Type.OBJECT; // see the comment in CompOp
                    }

                    List<Type> ret = new ArrayList<>();
                    for (int i = 0; i < 2; i++) {
                        Coercion c = Coercion.getCoercion(argTypes.get(i), commonTy);
                        assert c != null;
                        outCoercions.add(c);
                        ret.add(commonTy);
                    }

                    return ret;

                } else {

                    // further rules

                    if (lType.isDateTime()) {
                        if ((opName.equals("opAdd")
                                        && (rType.isString()
                                                || rType.isNumber()
                                                || rType == Type.NULL))
                                || (opName.equals("opSubtract")
                                        && (rType.isNumber() || rType == Type.NULL))) {

                            // date/time + string/number, or
                            // date/time - number

                            outCoercions.add(Coercion.Identity.getInstance(lType));
                            Coercion c = Coercion.getCoercion(rType, Type.BIGINT);
                            assert c != null;
                            outCoercions.add(c);

                            List<Type> ret = new ArrayList<>();
                            ret.add(lType);
                            ret.add(Type.BIGINT);

                            return ret;
                        }
                    }

                    if (rType.isDateTime()) {

                        if (opName.equals("opAdd")
                                && (lType.isString() || lType.isNumber() || lType == Type.NULL)) {

                            // string/number + date/time

                            Coercion c = Coercion.getCoercion(lType, Type.BIGINT);
                            assert c != null;
                            outCoercions.add(c);
                            outCoercions.add(Coercion.Identity.getInstance(rType));

                            List<Type> ret = new ArrayList<>();
                            ret.add(Type.BIGINT);
                            ret.add(rType);

                            return ret;
                        } else if (opName.equals("opSubtract") && lType == Type.NULL) {

                            Coercion c = Coercion.getCoercion(lType, rType);
                            assert c != null;
                            outCoercions.add(c);
                            outCoercions.add(Coercion.Identity.getInstance(rType));

                            List<Type> ret = new ArrayList<>();
                            ret.add(rType);
                            ret.add(rType);

                            return ret;
                        }
                    }

                    return null;
                }

            } else if (argTypes.size() == 1) {

                // unary -

                Type argType = argTypes.get(0);
                Type targetTy = getCommonTypeInner(argType, argType, arithOpCommonType);
                if (targetTy == null) {
                    return null;
                }

                if (targetTy == Type.NULL) {
                    targetTy = Type.OBJECT; // see the comment in CompOp
                }

                Coercion c = Coercion.getCoercion(argType, targetTy);
                assert c != null;
                outCoercions.add(c);

                List<Type> ret = new ArrayList<>();
                ret.add(targetTy);

                return ret;
            } else {
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
            }
        }
    },

    IntArithOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {

            if (argTypes.size() == 2) {

                // mod(%), div

                Type commonTy =
                        getCommonTypeInner(argTypes.get(0), argTypes.get(1), intArithOpCommonType);
                if (commonTy == null) {
                    return null;
                }

                if (commonTy == Type.NULL) {
                    commonTy = Type.OBJECT; // see the comment in CompOp
                }

                List<Type> ret = new ArrayList<>();
                for (int i = 0; i < 2; i++) {
                    Coercion c = Coercion.getCoercion(argTypes.get(i), commonTy);
                    assert c != null;
                    outCoercions.add(c);
                    ret.add(commonTy);
                }

                return ret;

            } else if (argTypes.size() == 1) {

                // ~

                Type argType = argTypes.get(0);
                Type targetTy = getCommonTypeInner(argType, argType, intArithOpCommonType);
                if (targetTy == null) {
                    return null;
                }

                if (targetTy == Type.NULL) {
                    targetTy = Type.OBJECT; // see the comment in CompOp
                }

                Coercion c = Coercion.getCoercion(argType, targetTy);
                assert c != null;
                outCoercions.add(c);

                List<Type> ret = new ArrayList<>();
                ret.add(targetTy);

                return ret;
            } else {
                assert false : "unreachable";
                throw new RuntimeException("unreachable");
            }
        }
    },

    LogicalOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {
            // and, or, xor, not
            return getCoercionsToFixedType(outCoercions, argTypes, Type.BOOLEAN);
        }
    },

    StringOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {
            // ||, like
            return getCoercionsToFixedType(outCoercions, argTypes, Type.STRING_ANY);
        }
    },

    BitOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {
            // <<, >>, &, ^, |
            return getCoercionsToFixedType(outCoercions, argTypes, Type.BIGINT);
        }
    },

    ObjectOp {
        public List<Type> getCoercions(
                List<Coercion> outCoercions, List<Type> argTypes, String opName) {
            // is-null
            return getCoercionsToFixedType(outCoercions, argTypes, Type.OBJECT);
        }
    };

    public static Type getCommonType(Type lType, Type rType) {
        return getCommonTypeInner(lType, rType, compOpCommonType);
    }

    public abstract List<Type> getCoercions(
            List<Coercion> outCoercions, List<Type> argTypes, String opName);

    // -----------------------------------------------------------------------
    // Setting for comparison operators

    static final Type[][] compOpCommonType = new Type[Type.BOUND_OF_IDX][Type.BOUND_OF_IDX];

    static {
        compOpCommonType[Type.IDX_NULL][Type.IDX_NULL] = Type.NULL;

        compOpCommonType[Type.IDX_OBJECT][Type.IDX_NULL] = Type.OBJECT;
        compOpCommonType[Type.IDX_OBJECT][Type.IDX_OBJECT] = Type.OBJECT;

        compOpCommonType[Type.IDX_BOOLEAN][Type.IDX_NULL] = Type.BOOLEAN;
        compOpCommonType[Type.IDX_BOOLEAN][Type.IDX_OBJECT] = Type.BOOLEAN;
        compOpCommonType[Type.IDX_BOOLEAN][Type.IDX_BOOLEAN] = Type.BOOLEAN;

        compOpCommonType[Type.IDX_STRING][Type.IDX_NULL] = Type.STRING_ANY;
        compOpCommonType[Type.IDX_STRING][Type.IDX_OBJECT] = Type.STRING_ANY;
        compOpCommonType[Type.IDX_STRING][Type.IDX_STRING] = Type.STRING_ANY;

        compOpCommonType[Type.IDX_SHORT][Type.IDX_NULL] = Type.SHORT;
        compOpCommonType[Type.IDX_SHORT][Type.IDX_OBJECT] = Type.SHORT;
        compOpCommonType[Type.IDX_SHORT][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_SHORT][Type.IDX_SHORT] = Type.SHORT;

        compOpCommonType[Type.IDX_INT][Type.IDX_NULL] = Type.INT;
        compOpCommonType[Type.IDX_INT][Type.IDX_OBJECT] = Type.INT;
        compOpCommonType[Type.IDX_INT][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_INT][Type.IDX_SHORT] = Type.INT;
        compOpCommonType[Type.IDX_INT][Type.IDX_INT] = Type.INT;

        compOpCommonType[Type.IDX_BIGINT][Type.IDX_NULL] = Type.BIGINT;
        compOpCommonType[Type.IDX_BIGINT][Type.IDX_OBJECT] = Type.BIGINT;
        compOpCommonType[Type.IDX_BIGINT][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_BIGINT][Type.IDX_SHORT] = Type.BIGINT;
        compOpCommonType[Type.IDX_BIGINT][Type.IDX_INT] = Type.BIGINT;
        compOpCommonType[Type.IDX_BIGINT][Type.IDX_BIGINT] = Type.BIGINT;

        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_NULL] = Type.NUMERIC_ANY;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_OBJECT] = Type.NUMERIC_ANY;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_SHORT] = Type.NUMERIC_ANY;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_INT] = Type.NUMERIC_ANY;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_BIGINT] = Type.NUMERIC_ANY;
        compOpCommonType[Type.IDX_NUMERIC][Type.IDX_NUMERIC] = Type.NUMERIC_ANY;

        compOpCommonType[Type.IDX_FLOAT][Type.IDX_NULL] = Type.FLOAT;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_OBJECT] = Type.FLOAT;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_SHORT] = Type.FLOAT;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_INT] = Type.FLOAT;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_BIGINT] = Type.FLOAT;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_NUMERIC] = Type.DOUBLE;
        compOpCommonType[Type.IDX_FLOAT][Type.IDX_FLOAT] = Type.FLOAT;

        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_NULL] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_OBJECT] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_STRING] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_SHORT] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_INT] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_BIGINT] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_NUMERIC] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_FLOAT] = Type.DOUBLE;
        compOpCommonType[Type.IDX_DOUBLE][Type.IDX_DOUBLE] = Type.DOUBLE;

        compOpCommonType[Type.IDX_DATE][Type.IDX_NULL] = Type.DATE;
        compOpCommonType[Type.IDX_DATE][Type.IDX_OBJECT] = Type.DATE;
        compOpCommonType[Type.IDX_DATE][Type.IDX_STRING] = Type.DATE;
        compOpCommonType[Type.IDX_DATE][Type.IDX_DATE] = Type.DATE;

        compOpCommonType[Type.IDX_TIME][Type.IDX_NULL] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_OBJECT] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_STRING] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_SHORT] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_INT] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_BIGINT] = Type.TIME;
        compOpCommonType[Type.IDX_TIME][Type.IDX_TIME] = Type.TIME;

        compOpCommonType[Type.IDX_DATETIME][Type.IDX_NULL] = Type.DATETIME;
        compOpCommonType[Type.IDX_DATETIME][Type.IDX_OBJECT] = Type.DATETIME;
        compOpCommonType[Type.IDX_DATETIME][Type.IDX_STRING] = Type.DATETIME;
        compOpCommonType[Type.IDX_DATETIME][Type.IDX_DATE] = Type.DATETIME;
        compOpCommonType[Type.IDX_DATETIME][Type.IDX_DATETIME] = Type.DATETIME;

        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_NULL] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_OBJECT] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_STRING] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_SHORT] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_INT] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_BIGINT] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_DATE] = Type.TIMESTAMP;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_DATETIME] = Type.DATETIME;
        compOpCommonType[Type.IDX_TIMESTAMP][Type.IDX_TIMESTAMP] = Type.TIMESTAMP;
    }

    // -----------------------------------------------------------------------
    // Setting for arithmetic operators (unary -, *, /, +, binary -)

    static final Type[][] arithOpCommonType = new Type[Type.BOUND_OF_IDX][Type.BOUND_OF_IDX];
    static final Type[][] subtractCommonTypeExt = new Type[Type.BOUND_OF_IDX][Type.BOUND_OF_IDX];

    static {
        arithOpCommonType[Type.IDX_NULL][Type.IDX_NULL] = Type.NULL;

        arithOpCommonType[Type.IDX_OBJECT][Type.IDX_NULL] = Type.OBJECT;
        arithOpCommonType[Type.IDX_OBJECT][Type.IDX_OBJECT] = Type.OBJECT;

        arithOpCommonType[Type.IDX_STRING][Type.IDX_STRING] = Type.DOUBLE;

        arithOpCommonType[Type.IDX_SHORT][Type.IDX_NULL] = Type.SHORT;
        arithOpCommonType[Type.IDX_SHORT][Type.IDX_OBJECT] = Type.SHORT;
        arithOpCommonType[Type.IDX_SHORT][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_SHORT][Type.IDX_SHORT] = Type.SHORT;

        arithOpCommonType[Type.IDX_INT][Type.IDX_NULL] = Type.INT;
        arithOpCommonType[Type.IDX_INT][Type.IDX_OBJECT] = Type.INT;
        arithOpCommonType[Type.IDX_INT][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_INT][Type.IDX_SHORT] = Type.INT;
        arithOpCommonType[Type.IDX_INT][Type.IDX_INT] = Type.INT;

        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_NULL] = Type.BIGINT;
        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_OBJECT] = Type.BIGINT;
        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_SHORT] = Type.BIGINT;
        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_INT] = Type.BIGINT;
        arithOpCommonType[Type.IDX_BIGINT][Type.IDX_BIGINT] = Type.BIGINT;

        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_NULL] = Type.NUMERIC_ANY;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_OBJECT] = Type.NUMERIC_ANY;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_SHORT] = Type.NUMERIC_ANY;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_INT] = Type.NUMERIC_ANY;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_BIGINT] = Type.NUMERIC_ANY;
        arithOpCommonType[Type.IDX_NUMERIC][Type.IDX_NUMERIC] = Type.NUMERIC_ANY;

        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_NULL] = Type.FLOAT;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_OBJECT] = Type.FLOAT;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_SHORT] = Type.FLOAT;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_INT] = Type.FLOAT;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_BIGINT] = Type.FLOAT;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_NUMERIC] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_FLOAT][Type.IDX_FLOAT] = Type.FLOAT;

        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_NULL] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_OBJECT] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_STRING] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_SHORT] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_INT] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_BIGINT] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_NUMERIC] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_FLOAT] = Type.DOUBLE;
        arithOpCommonType[Type.IDX_DOUBLE][Type.IDX_DOUBLE] = Type.DOUBLE;

        subtractCommonTypeExt[Type.IDX_DATE][Type.IDX_STRING] = Type.DATETIME;
        subtractCommonTypeExt[Type.IDX_DATE][Type.IDX_DATE] = Type.DATE;

        subtractCommonTypeExt[Type.IDX_TIME][Type.IDX_STRING] = Type.TIME;
        subtractCommonTypeExt[Type.IDX_TIME][Type.IDX_TIME] = Type.TIME;

        subtractCommonTypeExt[Type.IDX_DATETIME][Type.IDX_STRING] = Type.DATETIME;
        subtractCommonTypeExt[Type.IDX_DATETIME][Type.IDX_DATE] = Type.DATETIME;
        subtractCommonTypeExt[Type.IDX_DATETIME][Type.IDX_DATETIME] = Type.DATETIME;

        subtractCommonTypeExt[Type.IDX_TIMESTAMP][Type.IDX_STRING] = Type.DATETIME;
        subtractCommonTypeExt[Type.IDX_TIMESTAMP][Type.IDX_DATE] = Type.TIMESTAMP;
        subtractCommonTypeExt[Type.IDX_TIMESTAMP][Type.IDX_DATETIME] = Type.DATETIME;
        subtractCommonTypeExt[Type.IDX_TIMESTAMP][Type.IDX_TIMESTAMP] = Type.TIMESTAMP;
    }

    // -----------------------------------------------------------------------
    // Setting for integer arithmetic operators (mod, (integer) div, bit compliment)

    static final Type[][] intArithOpCommonType = new Type[Type.BOUND_OF_IDX][Type.BOUND_OF_IDX];

    static {
        intArithOpCommonType[Type.IDX_NULL][Type.IDX_NULL] = Type.NULL;

        intArithOpCommonType[Type.IDX_OBJECT][Type.IDX_NULL] = Type.OBJECT;
        intArithOpCommonType[Type.IDX_OBJECT][Type.IDX_OBJECT] = Type.OBJECT;

        intArithOpCommonType[Type.IDX_STRING][Type.IDX_STRING] = Type.BIGINT;

        intArithOpCommonType[Type.IDX_SHORT][Type.IDX_NULL] = Type.SHORT;
        intArithOpCommonType[Type.IDX_SHORT][Type.IDX_OBJECT] = Type.SHORT;
        intArithOpCommonType[Type.IDX_SHORT][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_SHORT][Type.IDX_SHORT] = Type.SHORT;

        intArithOpCommonType[Type.IDX_INT][Type.IDX_NULL] = Type.INT;
        intArithOpCommonType[Type.IDX_INT][Type.IDX_OBJECT] = Type.INT;
        intArithOpCommonType[Type.IDX_INT][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_INT][Type.IDX_SHORT] = Type.INT;
        intArithOpCommonType[Type.IDX_INT][Type.IDX_INT] = Type.INT;

        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_NULL] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_OBJECT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_SHORT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_INT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_BIGINT][Type.IDX_BIGINT] = Type.BIGINT;

        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_NULL] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_OBJECT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_SHORT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_INT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_BIGINT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_NUMERIC][Type.IDX_NUMERIC] = Type.BIGINT;

        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_NULL] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_OBJECT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_SHORT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_INT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_BIGINT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_NUMERIC] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_FLOAT][Type.IDX_FLOAT] = Type.BIGINT;

        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_NULL] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_OBJECT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_STRING] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_SHORT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_INT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_BIGINT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_NUMERIC] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_FLOAT] = Type.BIGINT;
        intArithOpCommonType[Type.IDX_DOUBLE][Type.IDX_DOUBLE] = Type.BIGINT;
    }

    private static List<Type> getCoercionsToFixedType(
            List<Coercion> outCoercions, List<Type> argTypes, Type targetType) {

        List<Type> ret = new ArrayList<>();
        for (Type t : argTypes) {
            Coercion c = Coercion.getCoercion(t, targetType);
            if (c == null) {
                return null;
            } else {
                ret.add(targetType);
                outCoercions.add(c);
            }
        }

        return ret;
    }

    private static Type getCommonTypeInner(Type lType, Type rType, Type[][]... table) {

        int lTypeIdx = lType.idx;
        assert lTypeIdx >= 0 && lTypeIdx < Type.BOUND_OF_IDX;
        int rTypeIdx = rType.idx;
        assert rTypeIdx >= 0 && rTypeIdx < Type.BOUND_OF_IDX;

        int row, col;
        if (lTypeIdx >= rTypeIdx) {
            row = lTypeIdx;
            col = rTypeIdx;
        } else {
            row = rTypeIdx;
            col = lTypeIdx;
        }

        int tables = table.length;
        for (int i = 0; i < tables; i++) {
            Type commonTy = table[i][row][col];
            if (commonTy != null) {
                return commonTy;
            }
        }

        return null;
    }
}
