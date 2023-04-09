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
    @Operator(coercionScheme=CoercionScheme.LogicalOp)
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
    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Short opNeg(Short l) {
        if (l == null) {
            return null;
        }
        return ((short) -l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Integer opNeg(Integer l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opNeg(Long l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static BigDecimal opNeg(BigDecimal l) {
        if (l == null) {
            return null;
        }
        return l.negate();
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Float opNeg(Float l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Double opNeg(Double l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Object opNeg(Object l) {
        if (l == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // bitwise compliment
    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Long opBitCompli(Short l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Long opBitCompli(Integer l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Long opBitCompli(Long l) {
        if (l == null) {
            return null;
        }
        return ~l;
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Object opBitCompli(Object l) {
        if (l == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // boolean and
    @Operator(coercionScheme=CoercionScheme.LogicalOp)
    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    // ====================================
    // boolean or
    @Operator(coercionScheme=CoercionScheme.LogicalOp)
    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    // ====================================
    // boolean xor
    @Operator(coercionScheme=CoercionScheme.LogicalOp)
    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    // ====================================
    // comparison equal

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Boolean l, Boolean r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(String l, String r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(BigDecimal l, BigDecimal r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Short l, Short r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Integer l, Integer r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Long l, Long r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Float l, Float r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Double l, Double r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Time l, Time r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Date l, Date r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Timestamp l, Timestamp r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison null safe equal

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Boolean l, Boolean r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(String l, String r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(BigDecimal l, BigDecimal r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Short l, Short r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Integer l, Integer r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Long l, Long r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Float l, Float r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Double l, Double r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Time l, Time r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Date l, Date r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Timestamp l, Timestamp r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Object l, Object r) {
        if (l == null) {
            return (r == null);
        } else if (r == null) {
            return false;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison not equal

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Boolean l, Boolean r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(String l, String r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(BigDecimal l, BigDecimal r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Short l, Short r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Integer l, Integer r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Long l, Long r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Float l, Float r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Double l, Double r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Time l, Time r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Date l, Date r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Timestamp l, Timestamp r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison less than or equal to (<=)

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Boolean l, Boolean r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(String l, String r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Short l, Short r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Integer l, Integer r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Long l, Long r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(BigDecimal l, BigDecimal r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Float l, Float r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Double l, Double r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Date l, Date r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Time l, Time r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Timestamp l, Timestamp r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLe(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison greater than or equal to (>=)
    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Boolean l, Boolean r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(String l, String r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Short l, Short r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Integer l, Integer r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Long l, Long r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(BigDecimal l, BigDecimal r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Float l, Float r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Double l, Double r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Date l, Date r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Time l, Time r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Timestamp l, Timestamp r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGe(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison less than (<)
    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Boolean l, Boolean r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(String l, String r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Short l, Short r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Integer l, Integer r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Long l, Long r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(BigDecimal l, BigDecimal r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Float l, Float r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Double l, Double r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Date l, Date r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Time l, Time r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Timestamp l, Timestamp r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opLt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // comparison greater than (>)
    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Boolean l, Boolean r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(String l, String r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Short l, Short r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Integer l, Integer r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Long l, Long r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(BigDecimal l, BigDecimal r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Float l, Float r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Double l, Double r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Date l, Date r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Time l, Time r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Timestamp l, Timestamp r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.CompOp)
    public static Boolean opGt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // between
    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Boolean o, Boolean lower, Boolean upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(String o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Short o, Short lower, Short upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Integer o, Integer lower, Integer upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Long o, Long lower, Long upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(BigDecimal o, BigDecimal lower, BigDecimal upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Float o, Float lower, Float upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Double o, Double lower, Double upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Date o, Date lower, Date upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Time o, Time lower, Time upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(Timestamp o, Timestamp lower, Timestamp upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme=CoercionScheme.BetweenOp)
    public static Boolean opBetween(ZonedDateTime o, ZonedDateTime lower, ZonedDateTime upper) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // in
    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Boolean o, Boolean... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Boolean p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(String o, String... list) {
        if (o == null || list == null) {
            return null;
        }
        for (String p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(BigDecimal o, BigDecimal... list) {
        if (o == null || list == null) {
            return null;
        }
        for (BigDecimal p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Short o, Short... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Short p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Integer o, Integer... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Integer p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Long o, Long... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Long p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Float o, Float... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Float p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Double o, Double... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Double p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Date o, Date... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Date p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Time o, Time... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Time p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(ZonedDateTime o, ZonedDateTime... list) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.InOp)
    public static Boolean opIn(Timestamp o, Timestamp... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Timestamp p : list) {
            if (Objects.equals(o, p)) {     // TODO: return null if p is null? check
                return true;
            }
        }
        return false;
    }

    // ====================================
    // *
    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Short opMult(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l * r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Integer opMult(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opMult(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static BigDecimal opMult(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.multiply(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Float opMult(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Double opMult(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Object opMult(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // /
    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Short opDiv(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Integer opDiv(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opDiv(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static BigDecimal opDiv(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.divide(r, BigDecimal.ROUND_HALF_UP);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Float opDiv(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Double opDiv(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Object opDiv(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // DIV
    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Short opDivInt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Integer opDivInt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Long opDivInt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Object opDivInt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // MOD
    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Short opMod(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l % r);
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Integer opMod(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Long opMod(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    @Operator(coercionScheme=CoercionScheme.IntArithOp)
    public static Object opMod(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // +
    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Short opAdd(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l + r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Integer opAdd(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opAdd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static BigDecimal opAdd(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.add(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Float opAdd(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Double opAdd(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Time opAdd(Time l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.plusSeconds(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Time opAdd(Long l, Time r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Date opAdd(Date l, Long r) {
        if (l == null || r == null) {
            return null;
        }

        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.plusDays(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Date opAdd(Long l, Date r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Timestamp opAdd(Timestamp l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.plus(r.longValue(), ChronoUnit.MILLIS));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Timestamp opAdd(Long l, Timestamp r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static ZonedDateTime opAdd(ZonedDateTime l, Long r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static ZonedDateTime opAdd(Long l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Object opAdd(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // -
    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Short opSubtract(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l - r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Integer opSubtract(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opSubtract(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static BigDecimal opSubtract(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.subtract(r);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Float opSubtract(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Double opSubtract(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opSubtract(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        LocalTime rlt = r.toLocalTime();
        return rlt.until(llt, ChronoUnit.SECONDS);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opSubtract(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        LocalDate rld = r.toLocalDate();
        return rld.until(lld, ChronoUnit.DAYS);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opSubtract(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        LocalDateTime rldt = r.toLocalDateTime();
        return rldt.until(lldt, ChronoUnit.MILLIS);
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Long opSubtract(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Time opSubtract(Time l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.minusSeconds(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Date opSubtract(Date l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.minusDays(r.longValue()));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Timestamp opSubtract(Timestamp l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.minus(r.longValue(), ChronoUnit.MILLIS));
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static ZonedDateTime opSubtract(ZonedDateTime l, Long r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new RuntimeException("unreachable");
    }

    @Operator(coercionScheme=CoercionScheme.ArithOp)
    public static Object opSubtract(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false: "unreachable";
        throw new RuntimeException("unreachable");
    }

    // ====================================
    // ||
    @Operator(coercionScheme=CoercionScheme.StringOp)
    public static String opConcat(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    // ====================================
    // <<
    @Operator(coercionScheme=CoercionScheme.BitOp)
    public static Long opBitShiftLeft(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l << r;
    }

    // ====================================
    // >>
    @Operator(coercionScheme=CoercionScheme.BitOp)
    public static Long opBitShiftRight(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l >> r;
    }

    // ====================================
    // &
    @Operator(coercionScheme=CoercionScheme.BitOp)
    public static Long opBitAnd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l & r;
    }

    // ====================================
    // ^
    @Operator(coercionScheme=CoercionScheme.BitOp)
    public static Long opBitXor(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l ^ r;
    }

    // ====================================
    // |
    @Operator(coercionScheme=CoercionScheme.BitOp)
    public static Long opBitOr(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l | r;
    }

    // ====================================
    // like
    @Operator(coercionScheme=CoercionScheme.StringOp)
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

    public static Date convDatetimeToDate(Timestamp e) {
        return null;    // TODO
    }
    public static Time convDatetimeToTime(Timestamp e) {
        return null;    // TODO
    }
    public static Timestamp convDatetimeToTimestamp(Timestamp e) {
        return null;    // TODO
    }
    public static String convDatetimeToString(Timestamp e) {
        return null;    // TODO
    }

    public static Timestamp convDateToDatetime(Date e) {
        return null;    // TODO
    }
    public static Timestamp convDateToTimestamp(Date e) {
        return null;    // TODO
    }
    public static String convDateToString(Date e) {
        return null;    // TODO
    }

    public static String convTimeToString(Time e) {
        return null;    // TODO
    }

    public static Timestamp convTimestampToDatetime(Timestamp e) {
        return null;    // TODO
    }
    public static Date convTimestampToDate(Timestamp e) {
        return null;    // TODO
    }
    public static Time convTimestampToTime(Timestamp e) {
        return null;    // TODO
    }
    public static String convTimestampToString(Timestamp e) {
        return null;    // TODO
    }

    public static Time convDoubleToTime(Double e) {
        return null;    // TODO
    }
    public static Timestamp convDoubleToTimestamp(Double e) {
        return null;    // TODO
    }
    public static Integer convDoubleToInt(Double e) {
        return null;    // TODO
    }
    public static Short convDoubleToShort(Double e) {
        return null;    // TODO
    }
    public static String convDoubleToString(Double e) {
        return null;    // TODO
    }
    public static Float convDoubleToFloat(Double e) {
        return null;    // TODO
    }
    public static BigDecimal convDoubleToNumeric(Double e) {
        return null;    // TODO
    }
    public static Long convDoubleToBigint(Double e) {
        return null;    // TODO
    }

    public static Time convFloatToTime(Float e) {
        return null;    // TODO
    }
    public static Timestamp convFloatToTimestamp(Float e) {
        return null;    // TODO
    }
    public static Integer convFloatToInt(Float e) {
        return null;    // TODO
    }
    public static Short convFloatToShort(Float e) {
        return null;    // TODO
    }
    public static String convFloatToString(Float e) {
        return null;    // TODO
    }
    public static Double convFloatToDouble(Float e) {
        return null;    // TODO
    }
    public static BigDecimal convFloatToNumeric(Float e) {
        return null;    // TODO
    }
    public static Long convFloatToBigint(Float e) {
        return null;    // TODO
    }

    public static Timestamp convNumericToTimestamp(BigDecimal e) {
        return null;    // TODO
    }
    public static Integer convNumericToInt(BigDecimal e) {
        return null;    // TODO
    }
    public static Short convNumericToShort(BigDecimal e) {
        return null;    // TODO
    }
    public static String convNumericToString(BigDecimal e) {
        return null;    // TODO
    }
    public static Double convNumericToDouble(BigDecimal e) {
        return null;    // TODO
    }
    public static Float convNumericToFloat(BigDecimal e) {
        return null;    // TODO
    }
    public static Long convNumericToBigint(BigDecimal e) {
        return null;    // TODO
    }

    public static Time convBigintToTime(Long e) {
        return null;    // TODO
    }
    public static Timestamp convBigintToTimestamp(Long e) {
        return null;    // TODO
    }
    public static Integer convBigintToInt(Long e) {
        return null;    // TODO
    }
    public static Short convBigintToShort(Long e) {
        return null;    // TODO
    }
    public static String convBigintToString(Long e) {
        return null;    // TODO
    }
    public static Double convBigintToDouble(Long e) {
        return null;    // TODO
    }
    public static Float convBigintToFloat(Long e) {
        return null;    // TODO
    }
    public static BigDecimal convBigintToNumeric(Long e) {
        return null;    // TODO
    }

    public static Time convIntToTime(Integer e) {
        return null;    // TODO
    }
    public static Timestamp convIntToTimestamp(Integer e) {
        return null;    // TODO
    }
    public static Short convIntToShort(Integer e) {
        return null;    // TODO
    }
    public static String convIntToString(Integer e) {
        return null;    // TODO
    }
    public static Double convIntToDouble(Integer e) {
        return null;    // TODO
    }
    public static Float convIntToFloat(Integer e) {
        return null;    // TODO
    }
    public static BigDecimal convIntToNumeric(Integer e) {
        return null;    // TODO
    }
    public static Long convIntToBigint(Integer e) {
        return null;    // TODO
    }

    public static Time convShortToTime(Short e) {
        return null;    // TODO
    }
    public static Timestamp convShortToTimestamp(Short e) {
        return null;    // TODO
    }
    public static Integer convShortToInt(Short e) {
        return null;    // TODO
    }
    public static String convShortToString(Short e) {
        return null;    // TODO
    }
    public static Double convShortToDouble(Short e) {
        return null;    // TODO
    }
    public static Float convShortToFloat(Short e) {
        return null;    // TODO
    }
    public static BigDecimal convShortToNumeric(Short e) {
        return null;    // TODO
    }
    public static Long convShortToBigint(Short e) {
        return null;    // TODO
    }

    public static Timestamp convStringToDatetime(String e) {
        return null;    // TODO
    }
    public static Date convStringToDate(String e) {
        return null;    // TODO
    }
    public static Time convStringToTime(String e) {
        return null;    // TODO
    }
    public static Timestamp convStringToTimestamp(String e) {
        return null;    // TODO
    }
    public static Integer convStringToInt(String e) {
        return null;    // TODO
    }
    public static Short convStringToShort(String e) {
        return null;    // TODO
    }
    public static Double convStringToDouble(String e) {
        return null;    // TODO
    }
    public static Float convStringToFloat(String e) {
        return null;    // TODO
    }
    public static BigDecimal convStringToNumeric(String e) {
        return null;    // TODO
    }
    public static Long convStringToBigint(String e) {
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
            return false;
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
}
