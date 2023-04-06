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

package com.cubrid.plcsql.predefined.sp;

import com.cubrid.plcsql.builtin.DBMS_OUTPUT;
import com.cubrid.plcsql.compiler.CoercionScheme;
import com.cubrid.plcsql.compiler.annotation.Operator;
import java.math.BigDecimal;
import java.sql.*;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;
import java.time.temporal.ChronoUnit;
import java.util.Objects;
import java.util.regex.PatternSyntaxException;

public class SpLib {

    public static class $APP_ERROR extends RuntimeException {}

    public static class CASE_NOT_FOUND extends RuntimeException {}

    public static class CURSOR_ALREADY_OPEN extends RuntimeException {}

    public static class DUP_VAL_ON_INDEX extends RuntimeException {}

    public static class INVALID_CURSOR extends RuntimeException {}

    public static class LOGIN_DENIED extends RuntimeException {}

    public static class NO_DATA_FOUND extends RuntimeException {}

    public static class PROGRAM_ERROR extends RuntimeException {}

    public static class ROWTYPE_MISMATCH extends RuntimeException {}

    public static class STORAGE_ERROR extends RuntimeException {}

    public static class TOO_MANY_ROWS extends RuntimeException {}

    public static class VALUE_ERROR extends RuntimeException {}

    public static class ZERO_DIVIDE extends RuntimeException {}

    public static String SQLERRM = null;
    public static Integer SQLCODE = null;
    public static Date SYSDATE = null;

    public static Object raiseCaseNotFound() {
        throw new CASE_NOT_FOUND();
    }

    public static void PUT_LINE(Object s) {
        DBMS_OUTPUT.putLine(s.toString());
    }

    public static class Query {
        public final String query;
        public ResultSet rs;

        public Query(String query) {
            this.query = query;
        }

        public void open(Connection conn, Object... val) throws Exception {
            if (isOpen()) {
                throw new RuntimeException("already open");
            }
            PreparedStatement pstmt = conn.prepareStatement(query);
            for (int i = 0; i < val.length; i++) {
                pstmt.setObject(i + 1, val[i]);
            }
            rs = pstmt.executeQuery();
        }

        public void close() throws Exception {
            if (rs != null) {
                Statement stmt = rs.getStatement();
                if (stmt != null) {
                    stmt.close();
                }
                rs = null;
            }
        }

        public boolean isOpen() throws Exception {
            return (rs != null && !rs.isClosed());
        }

        public boolean found() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow() > 0;
        }

        public boolean notFound() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow() == 0;
        }

        public int rowCount() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow();
        }
    }

    // ------------------------------------
    // operators
    // ------------------------------------

    // ====================================
    // boolean not
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opNot(Boolean l) {
        if (l == null) {
            return null;
        }
        return !l;
    }

    // ====================================
    // is null
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opIsNull(Object l) {
        return (l == null);
    }

    // ====================================
    // arithmetic negative
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Short opNeg(Short l) {
        if (l == null) {
            return null;
        }
        return ((short) -l);
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Integer opNeg(Integer l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Long opNeg(Long l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static BigDecimal opNeg(BigDecimal l) {
        if (l == null) {
            return null;
        }
        return l.negate();
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Float opNeg(Float l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Double opNeg(Double l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    // ====================================
    // bitwise compliment
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Long opBitCompli(Short l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Long opBitCompli(Integer l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Long opBitCompli(Long l) {
        if (l == null) {
            return null;
        }
        return ~l;
    }

    // ====================================
    // boolean and
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    // ====================================
    // boolean or
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    // ====================================
    // boolean xor
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    // ====================================
    // comparison equal

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Boolean l, Boolean r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(String l, String r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(BigDecimal l, BigDecimal r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Short l, Short r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Integer l, Integer r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Long l, Long r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Float l, Float r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Double l, Double r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Time l, Time r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Date l, Date r) {
        return commonOpEq(l, r);
    }

    /*
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(ZonedDateTime l, ZonedDateTime r) {
        return commonOpEq(l, r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Timestamp l, Timestamp r) {
        return commonOpEq(l, r);
    }

    /* TODO: restore later
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(Set l, Set r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(MultiSet l, MultiSet r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opEq(List l, List r) {
        return commonOpEq(l, r);
    }
     */

    // ====================================
    // comparison null safe equal

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Boolean l, Boolean r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(String l, String r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(BigDecimal l, BigDecimal r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Short l, Short r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Integer l, Integer r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Long l, Long r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Float l, Float r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Double l, Double r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Time l, Time r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Date l, Date r) {
        return commonOpNullSafeEq(l, r);
    }

    /*
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(ZonedDateTime l, ZonedDateTime r) {
        return commonOpNullSafeEq(l, r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Timestamp l, Timestamp r) {
        return commonOpNullSafeEq(l, r);
    }

    /* TODO: restore later
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(Set l, Set r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(MultiSet l, MultiSet r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNullSafeEq(List l, List r) {
        return commonOpNullSafeEq(l, r);
    }
     */

    // ====================================
    // comparison not equal

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Boolean l, Boolean r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(String l, String r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(BigDecimal l, BigDecimal r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Short l, Short r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Integer l, Integer r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Long l, Long r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Float l, Float r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Double l, Double r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Time l, Time r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Date l, Date r) {
        return commonOpNeq(l, r);
    }

    /*
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(ZonedDateTime l, ZonedDateTime r) {
        return commonOpNeq(l, r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Timestamp l, Timestamp r) {
        return commonOpNeq(l, r);
    }

    /* TODO: restore later
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(Set l, Set r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(MultiSet l, MultiSet r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opNeq(List l, List r) {
        return commonOpNeq(l, r);
    }
     */

    // ====================================
    // comparison less than or equal to (<=)

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(String l, String r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Short l, Short r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Integer l, Integer r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Long l, Long r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(BigDecimal l, BigDecimal r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Float l, Float r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Double l, Double r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Date l, Date r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Time l, Time r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Timestamp l, Timestamp r) {
        return commonOpLe(l, r);
    }

    /*
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(ZonedDateTime l, ZonedDateTime r) {
        return commonOpLe(l, r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    /* TODO: restore later
    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static Boolean opLe(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    @Operator(coercionScheme=CoercionScheme.ComparisonOperator)
    public static <E extends Comparable<E>> Boolean opLe(List<E> l, List<E> r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) <= 0;
    }
     */

    // ====================================
    // comparison greater than or equal to (>=)
    public static Boolean opGe(String l, String r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Short l, Short r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Integer l, Integer r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Long l, Long r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(BigDecimal l, BigDecimal r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Float l, Float r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Double l, Double r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Date l, Date r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Time l, Time r) {
        return commonOpGe(l, r);
    }

    public static Boolean opGe(Timestamp l, Timestamp r) {
        return commonOpGe(l, r);
    }

    /*
    public static Boolean opGe(ZonedDateTime l, ZonedDateTime r) {
        return commonOpGe(l, r);
    }
     */
    public static Boolean opGe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    /* TODO: restore later
    public static Boolean opGe(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareSets(l, r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opGe(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static <E extends Comparable<E>> Boolean opGe(List<E> l, List<E> r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) >= 0;
    }
     */

    // ====================================
    // comparison less than (<)
    public static Boolean opLt(String l, String r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Short l, Short r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Integer l, Integer r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Long l, Long r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(BigDecimal l, BigDecimal r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Float l, Float r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Double l, Double r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Date l, Date r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Time l, Time r) {
        return commonOpLt(l, r);
    }

    public static Boolean opLt(Timestamp l, Timestamp r) {
        return commonOpLt(l, r);
    }

    /*
    public static Boolean opLt(ZonedDateTime l, ZonedDateTime r) {
        return commonOpLt(l, r);
    }
     */
    public static Boolean opLt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    /*
    public static Boolean opLt(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareSets(l, r) == SetOrder.INCLUDED);
    }

    public static Boolean opLt(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareMultiSets(l, r) == SetOrder.INCLUDED);
    }

    public static <E extends Comparable<E>> Boolean opLt(List<E> l, List<E> r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) < 0;
    }
     */

    // ====================================
    // comparison greater than (>)
    public static Boolean opGt(String l, String r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Short l, Short r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Integer l, Integer r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Long l, Long r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(BigDecimal l, BigDecimal r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Float l, Float r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Double l, Double r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Date l, Date r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Time l, Time r) {
        return commonOpGt(l, r);
    }

    public static Boolean opGt(Timestamp l, Timestamp r) {
        return commonOpGt(l, r);
    }

    /*
    public static Boolean opGt(ZonedDateTime l, ZonedDateTime r) {
        return commonOpGt(l, r);
    }
     */
    public static Boolean opGt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    /* TODO: restore later
    public static Boolean opGt(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareSets(l, r) == SetOrder.INCLUDING);
    }

    public static Boolean opGt(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareMultiSets(l, r) == SetOrder.INCLUDING);
    }

    public static <E extends Comparable<E>> Boolean opGt(List<E> l, List<E> r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) > 0;
    }
     */

    /*
    // ====================================
    // comparison set equal
    public static Boolean opSetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2MultiSet(r)) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), set2MultiSet(r)) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), r) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), r) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), list2MultiSet(r)) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2MultiSet(r)) == SetOrder.EQUAL;
    }

    public static Boolean opSetEq(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) == 0;
    }

    // ====================================
    // comparison set not equal
    public static Boolean opSetNeq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2MultiSet(r)) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), set2MultiSet(r)) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), r) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), r) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), list2MultiSet(r)) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2MultiSet(r)) != SetOrder.EQUAL;
    }

    public static Boolean opSetNeq(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), list2MultiSet(r)) != SetOrder.EQUAL;
    }

    // ====================================
    // comparison superset
    public static Boolean opSuperset(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareSets(l, r) == SetOrder.INCLUDING);
    }

    public static Boolean opSuperset(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2MultiSet(r)) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), set2MultiSet(r)) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), r) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), r) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), list2MultiSet(r)) == SetOrder.INCLUDING;
    }

    public static Boolean opSuperset(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2MultiSet(r)) == SetOrder.INCLUDING;
    }

    // ====================================
    // comparison subset
    public static Boolean opSubset(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return (compareSets(l, r) == SetOrder.INCLUDED);
    }

    public static Boolean opSubset(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2MultiSet(r)) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), set2MultiSet(r)) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), r) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2MultiSet(l), r) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2MultiSet(l), list2MultiSet(r)) == SetOrder.INCLUDED;
    }

    public static Boolean opSubset(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2MultiSet(r)) == SetOrder.INCLUDED;
    }

    // ====================================
    // comparison superset or equal
    public static Boolean opSupersetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareSets(l, r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, set2MultiSet(r));
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(list2MultiSet(l), set2MultiSet(r));
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(set2MultiSet(l), r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(list2MultiSet(l), r);
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(set2MultiSet(l), list2MultiSet(r));
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    public static Boolean opSupersetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, list2MultiSet(r));
        return (o == SetOrder.INCLUDING || o == SetOrder.EQUAL);
    }

    // ====================================
    // comparison subset or equal
    public static Boolean opSubsetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, set2MultiSet(r));
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(list2MultiSet(l), set2MultiSet(r));
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(set2MultiSet(l), r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(list2MultiSet(l), r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(set2MultiSet(l), list2MultiSet(r));
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }

    public static Boolean opSubsetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, list2MultiSet(r));
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }
    */

    // ====================================
    // between
    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Boolean o, Boolean lower, Boolean upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(String o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Short o, Short lower, Short upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Integer o, Integer lower, Integer upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Long o, Long lower, Long upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(BigDecimal o, BigDecimal lower, BigDecimal upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Float o, Float lower, Float upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Double o, Double lower, Double upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Date o, Date lower, Date upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Time o, Time lower, Time upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(Timestamp o, Timestamp lower, Timestamp upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    /*
    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(ZonedDateTime o, ZonedDateTime lower, ZonedDateTime upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
     */
    @Operator(coercionScheme=CoercionScheme.BetweenOperator)
    public static Boolean opBetween(ZonedDateTime o, ZonedDateTime lower, ZonedDateTime upper) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // in
    @Operator(coercionScheme=CoercionScheme.InOperator)
    public static Boolean opIn(Object o, Object... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Object p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    // ====================================
    // *
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opMult(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l * r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opMult(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opMult(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static BigDecimal opMult(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.multiply(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Float opMult(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Double opMult(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }
    /*
    // sets
    public static Set opMult(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectSets(l, r);
    }

    public static MultiSet opMult(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(l, set2MultiSet(r));
    }

    public static MultiSet opMult(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(list2MultiSet(l), set2MultiSet(r));
    }

    public static MultiSet opMult(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(set2MultiSet(l), r);
    }

    public static MultiSet opMult(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(l, r);
    }

    public static MultiSet opMult(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(list2MultiSet(l), r);
    }

    public static MultiSet opMult(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(set2MultiSet(l), list2MultiSet(r));
    }

    public static MultiSet opMult(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(l, list2MultiSet(r));
    }

    public static MultiSet opMult(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return intersectMultiSets(list2MultiSet(l), list2MultiSet(r));
    }
    */

    // ====================================
    // /
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opDiv(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opDiv(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opDiv(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static BigDecimal opDiv(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.divide(r, BigDecimal.ROUND_HALF_UP);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Float opDiv(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Double opDiv(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    // ====================================
    // DIV
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opDivInt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opDivInt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opDivInt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    // ====================================
    // MOD
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opMod(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l % r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opMod(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opMod(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    // ====================================
    // +
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opAdd(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l + r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opAdd(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opAdd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static BigDecimal opAdd(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.add(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Float opAdd(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Double opAdd(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Time opAdd(Time l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.plusSeconds(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Time opAdd(Long l, Time r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Date opAdd(Date l, Long r) {
        if (l == null || r == null) {
            return null;
        }

        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.plusDays(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Date opAdd(Long l, Date r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Timestamp opAdd(Timestamp l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.plus(r.longValue(), ChronoUnit.MILLIS));
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Timestamp opAdd(Long l, Timestamp r) {
        return opAdd(r, l);
    }

    /*
    public static ZonedDateTime opAdd(ZonedDateTime l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l.plusSeconds(r.longValue());
    }
     */
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static ZonedDateTime opAdd(ZonedDateTime l, Long r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Timestamp opAdd(Long l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    /*
    // sets
    public static Set opAdd(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return unionSets(l, r);
    }

    public static MultiSet opAdd(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(l, set2MultiSet(r));
    }

    public static MultiSet opAdd(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(list2MultiSet(l), set2MultiSet(r));
    }

    public static MultiSet opAdd(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(set2MultiSet(l), r);
    }

    public static MultiSet opAdd(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(l, r);
    }

    public static MultiSet opAdd(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(list2MultiSet(l), r);
    }

    public static MultiSet opAdd(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(set2MultiSet(l), list2MultiSet(r));
    }

    public static MultiSet opAdd(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return unionMultiSets(l, list2MultiSet(r));
    }

    public static List opAdd(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return concatLists(l, r);
    }
    */

    // ====================================
    // -
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Short opSubtract(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l - r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Integer opSubtract(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static BigDecimal opSubtract(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.subtract(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Float opSubtract(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Double opSubtract(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        LocalTime rlt = r.toLocalTime();
        return rlt.until(llt, ChronoUnit.SECONDS);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        LocalDate rld = r.toLocalDate();
        return rld.until(lld, ChronoUnit.DAYS);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        LocalDateTime rldt = r.toLocalDateTime();
        return rldt.until(lldt, ChronoUnit.MILLIS);
    }

    /*
    public static Long opSubtract(ZonedDateTime l, ZonedDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return r.until(l, ChronoUnit.SECONDS);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Time opSubtract(Time l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.minusSeconds(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Date opSubtract(Date l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.minusDays(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Timestamp opSubtract(Timestamp l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.minus(r.longValue(), ChronoUnit.MILLIS));
    }

    /*
    public static ZonedDateTime opSubtract(ZonedDateTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.minusSeconds(r.longValue());
    }

    public static Long opSubtract(LocalDate l, ZonedDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(ZonedDateTime.of(l, LocalTime.MIN, r.getZone()), r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static ZonedDateTime opSubtract(ZonedDateTime l, Integer r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Date l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Date l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }

        LocalDate lld = l.toLocalDate();
        return opSubtract(Timestamp.valueOf(LocalDateTime.of(lld, LocalTime.MIN)), r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Timestamp l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate rld = r.toLocalDate();
        return opSubtract(l, Timestamp.valueOf(LocalDateTime.of(rld, LocalTime.MIN)));
    }

    /*
    public static Long opSubtract(LocalDateTime l, ZonedDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(l, r.toLocalDateTime());
    }

    public static Long opSubtract(ZonedDateTime l, LocalDate r) {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(l, ZonedDateTime.of(r, LocalTime.MIN, l.getZone()));
    }

    public static Long opSubtract(ZonedDateTime l, LocalDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(l.toLocalDateTime(), r);
    }
     */
    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(Timestamp l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(ZonedDateTime l, Date r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithmeticOperator)
    public static Long opSubtract(ZonedDateTime l, Timestamp r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }
    /*
    // sets
    public static Set opSubtract(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return diffSets(l, r);
    }

    public static MultiSet opSubtract(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(l, set2MultiSet(r));
    }

    public static MultiSet opSubtract(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(list2MultiSet(l), set2MultiSet(r));
    }

    public static MultiSet opSubtract(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(set2MultiSet(l), r);
    }

    public static MultiSet opSubtract(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(l, r);
    }

    public static MultiSet opSubtract(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(list2MultiSet(l), r);
    }

    public static MultiSet opSubtract(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(set2MultiSet(l), list2MultiSet(r));
    }

    public static MultiSet opSubtract(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(l, list2MultiSet(r));
    }

    public static MultiSet opSubtract(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return diffMultiSets(list2MultiSet(l), list2MultiSet(r));
    }
     */

    // ====================================
    // ||
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static String opConcat(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.toString() + r.toString();
    }

    // ====================================
    // <<
    @Operator(coercionScheme=CoercionScheme.BitOperator)
    public static Long opBitShiftLeft(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l << r;
    }

    // ====================================
    // >>
    @Operator(coercionScheme=CoercionScheme.BitOperator)
    public static Long opBitShiftRight(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l >> r;
    }

    // ====================================
    // &
    @Operator(coercionScheme=CoercionScheme.BitOperator)
    public static Long opBitAnd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l & r;
    }

    // ====================================
    // ^
    @Operator(coercionScheme=CoercionScheme.BitOperator)
    public static Long opBitXor(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l ^ r;
    }

    // ====================================
    // |
    @Operator(coercionScheme=CoercionScheme.BitOperator)
    public static Long opBitOr(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l | r;
    }

    // ====================================
    // like
    @Operator(coercionScheme=CoercionScheme.Individual)
    public static Boolean opLike(String s, String pattern, String escape) {
        assert pattern != null;
        assert escape == null || escape.length() == 1;

        if (s == null) {
            return null;
        }

        String regex = getRegexForLike(pattern, escape);
        try {
            return s.matches(regex);
        } catch (PatternSyntaxException e) {
            assert false;
            throw new RuntimeException("unreachable");
        }
    }

    // ------------------------------------
    // coercions
    // ------------------------------------

    public static Date castDatetimeToDate(Timestamp e) {
        return null;    // TODO
    }
    public static Time castDatetimeToTime(Timestamp e) {
        return null;    // TODO
    }
    public static Timestamp castDatetimeToTimestamp(Timestamp e) {
        return null;    // TODO
    }
    public static String castDatetimeToString(Timestamp e) {
        return null;    // TODO
    }

    public static Timestamp castDateToDatetime(Date e) {
        return null;    // TODO
    }
    public static Timestamp castDateToTimestamp(Date e) {
        return null;    // TODO
    }
    public static String castDateToString(Date e) {
        return null;    // TODO
    }

    public static String castTimeToString(Time e) {
        return null;    // TODO
    }

    public static Timestamp castTimestampToDatetime(Timestamp e) {
        return null;    // TODO
    }
    public static Date castTimestampToDate(Timestamp e) {
        return null;    // TODO
    }
    public static Time castTimestampToTime(Timestamp e) {
        return null;    // TODO
    }
    public static String castTimestampToString(Timestamp e) {
        return null;    // TODO
    }

    public static Time castDoubleToTime(Double e) {
        return null;    // TODO
    }
    public static Timestamp castDoubleToTimestamp(Double e) {
        return null;    // TODO
    }
    public static Integer castDoubleToInt(Double e) {
        return null;    // TODO
    }
    public static Short castDoubleToShort(Double e) {
        return null;    // TODO
    }
    public static String castDoubleToString(Double e) {
        return null;    // TODO
    }
    public static Float castDoubleToFloat(Double e) {
        return null;    // TODO
    }
    public static BigDecimal castDoubleToNumeric(Double e) {
        return null;    // TODO
    }
    public static Long castDoubleToBigint(Double e) {
        return null;    // TODO
    }

    public static Time castFloatToTime(Float e) {
        return null;    // TODO
    }
    public static Timestamp castFloatToTimestamp(Float e) {
        return null;    // TODO
    }
    public static Integer castFloatToInt(Float e) {
        return null;    // TODO
    }
    public static Short castFloatToShort(Float e) {
        return null;    // TODO
    }
    public static String castFloatToString(Float e) {
        return null;    // TODO
    }
    public static Double castFloatToDouble(Float e) {
        return null;    // TODO
    }
    public static BigDecimal castFloatToNumeric(Float e) {
        return null;    // TODO
    }
    public static Long castFloatToBigint(Float e) {
        return null;    // TODO
    }

    public static Timestamp castNumericToTimestamp(BigDecimal e) {
        return null;    // TODO
    }
    public static Integer castNumericToInt(BigDecimal e) {
        return null;    // TODO
    }
    public static Short castNumericToShort(BigDecimal e) {
        return null;    // TODO
    }
    public static String castNumericToString(BigDecimal e) {
        return null;    // TODO
    }
    public static Double castNumericToDouble(BigDecimal e) {
        return null;    // TODO
    }
    public static Float castNumericToFloat(BigDecimal e) {
        return null;    // TODO
    }
    public static Long castNumericToBigint(BigDecimal e) {
        return null;    // TODO
    }

    public static Time castBigintToTime(Long e) {
        return null;    // TODO
    }
    public static Timestamp castBigintToTimestamp(Long e) {
        return null;    // TODO
    }
    public static Integer castBigintToInt(Long e) {
        return null;    // TODO
    }
    public static Short castBigintToShort(Long e) {
        return null;    // TODO
    }
    public static String castBigintToString(Long e) {
        return null;    // TODO
    }
    public static Double castBigintToDouble(Long e) {
        return null;    // TODO
    }
    public static Float castBigintToFloat(Long e) {
        return null;    // TODO
    }
    public static BigDecimal castBigintToNumeric(Long e) {
        return null;    // TODO
    }

    public static Time castIntToTime(Integer e) {
        return null;    // TODO
    }
    public static Timestamp castIntToTimestamp(Integer e) {
        return null;    // TODO
    }
    public static Short castIntToShort(Integer e) {
        return null;    // TODO
    }
    public static String castIntToString(Integer e) {
        return null;    // TODO
    }
    public static Double castIntToDouble(Integer e) {
        return null;    // TODO
    }
    public static Float castIntToFloat(Integer e) {
        return null;    // TODO
    }
    public static BigDecimal castIntToNumeric(Integer e) {
        return null;    // TODO
    }
    public static Long castIntToBigint(Integer e) {
        return null;    // TODO
    }

    public static Time castShortToTime(Short e) {
        return null;    // TODO
    }
    public static Timestamp castShortToTimestamp(Short e) {
        return null;    // TODO
    }
    public static Integer castShortToInt(Short e) {
        return null;    // TODO
    }
    public static String castShortToString(Short e) {
        return null;    // TODO
    }
    public static Double castShortToDouble(Short e) {
        return null;    // TODO
    }
    public static Float castShortToFloat(Short e) {
        return null;    // TODO
    }
    public static BigDecimal castShortToNumeric(Short e) {
        return null;    // TODO
    }
    public static Long castShortToBigint(Short e) {
        return null;    // TODO
    }

    public static Timestamp castStringToDatetime(String e) {
        return null;    // TODO
    }
    public static Date castStringToDate(String e) {
        return null;    // TODO
    }
    public static Time castStringToTime(String e) {
        return null;    // TODO
    }
    public static Timestamp castStringToTimestamp(String e) {
        return null;    // TODO
    }
    public static Integer castStringToInt(String e) {
        return null;    // TODO
    }
    public static Short castStringToShort(String e) {
        return null;    // TODO
    }
    public static Double castStringToDouble(String e) {
        return null;    // TODO
    }
    public static Float castStringToFloat(String e) {
        return null;    // TODO
    }
    public static BigDecimal castStringToNumeric(String e) {
        return null;    // TODO
    }
    public static Long castStringToBigint(String e) {
        return null;    // TODO
    }

    // ------------------------------------------------
    // Private
    // ------------------------------------------------

    private static Boolean commonOpEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.equals(r);
    }

    private static Boolean commonOpNullSafeEq(Object l, Object r) {
        if (l == null) {
            return (r == null);
        } else if (r == null) {
            return (l == null);
        }
        return l.equals(r);
    }

    private static Boolean commonOpNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return !l.equals(r);
    }

    private static Boolean commonOpLe(Comparable l, Comparable r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }

    private static Boolean commonOpLt(Comparable l, Comparable r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }

    private static Boolean commonOpGe(Comparable l, Comparable r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }

    private static Boolean commonOpGt(Comparable l, Comparable r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }

    private static String getRegexForLike(String pattern, String escape) {

        StringBuffer sbuf = new StringBuffer();

        int len = pattern.length();
        if (escape == null) {
            for (int i = 0; i < len; i++) {
                char c = pattern.charAt(i);
                if (c == '%') {
                    sbuf.append(".*");
                } else if (c == '_') {
                    sbuf.append(".");
                } else {
                    sbuf.append(c);
                }
            }
        } else {
            char esc = escape.charAt(0);
            for (int i = 0; i < len; i++) {
                char c = pattern.charAt(i);
                if (esc == c) {
                    if (i + 1 == len) {
                        sbuf.append(c); // append the escape character at the end of the pattern as
                        // CUBRID does
                    } else {
                        i++;
                        sbuf.append(
                                pattern.charAt(
                                        i)); // append it whether it is one of '%', '_', or the
                        // escape char.
                    }
                } else {
                    if (c == '%') {
                        sbuf.append(".*");
                    } else if (c == '_') {
                        sbuf.append(".");
                    } else {
                        sbuf.append(c);
                    }
                }
            }
        }

        return sbuf.toString();
    }

    /* TODO: restore later
    // set and multiset ordering
    enum SetOrder {
        EQUAL,
        INCLUDING,
        INCLUDED,
        NONE,
    }

    static SetOrder compareSets(Set l, Set r) {
        assert l != null && r != null;

        if (l.equals(r)) {
            return SetOrder.EQUAL;
        }
        if (l.containsAll(r)) {
            return SetOrder.INCLUDING;
        }
        if (r.containsAll(l)) {
            return SetOrder.INCLUDED;
        }

        return SetOrder.NONE;
    }

    static SetOrder compareMultiSets(MultiSet l, MultiSet r) {
        assert l != null && r != null;

        if (l.equals(r)) {
            return SetOrder.EQUAL;
        } else {
            boolean broken = false;
            if (l.containsAll(r)) {
                Set s = r.uniqueSet();

                for (Object o : s) {
                    int ln = l.getCount(o);
                    int rn = r.getCount(o);
                    if (ln < rn) {
                        broken = true;
                        break;
                    }
                }
                if (!broken) {
                    return SetOrder.INCLUDING;
                }
            }

            broken = false;
            if (r.containsAll(l)) {
                Set s = l.uniqueSet();
                for (Object o : s) {
                    int ln = l.getCount(o);
                    int rn = r.getCount(o);
                    if (ln > rn) {
                        broken = true;
                        break;
                    }
                }
                if (!broken) {
                    return SetOrder.INCLUDED;
                }
            }

            return SetOrder.NONE;
        }
    }

    static <E extends Comparable<E>> int compareLists(List<E> l, List<E> r) {
        assert l != null && r != null;

        // lexicographic order

        int ln = l.size();
        int rn = r.size();
        int common = Math.min(ln, rn);

        for (int i = 0; i < common; i++) {
            E le = l.get(i);
            E re = r.get(i);
            int sign = le.compareTo(re);
            if (sign > 0) {
                return 1;
            }
            if (sign < 0) {
                return -1;
            }
        }

        // every pair of elements upto first 'common' ones has the same elements
        return (ln - rn);
    }

    static MultiSet set2MultiSet(Set s) {
        assert s != null;
        return new HashMultiSet(s);
    }

    static MultiSet list2MultiSet(List l) {
        assert l != null;
        return new HashMultiSet(
                l); // TODO: check if the elements duplicate in the list has count larger than 1
    }

    static List concatLists(List l, List r) {
        ArrayList ret = new ArrayList();
        ret.addAll(l);
        ret.addAll(r);
        return ret;
    }

    static Set unionSets(Set l, Set r) {
        HashSet ret = new HashSet();
        ret.addAll(l);
        ret.addAll(r);
        return ret;
    }

    static Set diffSets(Set l, Set r) {
        HashSet ret = new HashSet();
        ret.addAll(l);
        ret.removeAll(r);
        return ret;
    }

    static Set intersectSets(Set l, Set r) {
        HashSet ret = new HashSet();
        ret.addAll(l);
        ret.retainAll(r);
        return ret;
    }

    static MultiSet unionMultiSets(MultiSet l, MultiSet r) {
        HashMultiSet ret = new HashMultiSet(l);
        ret.addAll(r);
        return ret;
    }

    static MultiSet diffMultiSets(MultiSet l, MultiSet r) {
        HashMultiSet ret = new HashMultiSet(l);
        for (Object o : r) {
            ret.remove(o);
        }
        return ret;
    }

    static MultiSet intersectMultiSets(MultiSet l, MultiSet r) {
        HashMultiSet ret = new HashMultiSet();
        Set s = l.uniqueSet();
        for (Object o : s) {
            int ln = l.getCount(o);
            int rn = r.getCount(o);
            int min = Math.min(ln, rn);
            if (min > 0) {
                ret.add(o, min);
            }
        }
        return ret;
    }
     */

}
