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

import com.cubrid.jsp.Server;
import com.cubrid.plcsql.builtin.DBMS_OUTPUT;
import com.cubrid.plcsql.compiler.CoercionScheme;
import com.cubrid.plcsql.compiler.DateTimeParser;
import com.cubrid.plcsql.compiler.annotation.Operator;
import com.cubrid.plcsql.predefined.PlcsqlRuntimeError;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.sql.*;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;
import java.time.temporal.ChronoUnit;
import java.util.regex.PatternSyntaxException;

public class SpLib {

    // builtin exceptions
    public static class CASE_NOT_FOUND extends PlcsqlRuntimeError {
        public CASE_NOT_FOUND() {
            super(CODE_CASE_NOT_FOUND, MSG_CASE_NOT_FOUND);
        }
    }

    public static class CURSOR_ALREADY_OPEN extends PlcsqlRuntimeError {
        public CURSOR_ALREADY_OPEN() {
            super(CODE_CURSOR_ALREADY_OPEN, MSG_CURSOR_ALREADY_OPEN);
        }
    }

    public static class INVALID_CURSOR extends PlcsqlRuntimeError {
        public INVALID_CURSOR() {
            super(CODE_INVALID_CURSOR, MSG_INVALID_CURSOR);
        }
    }

    public static class NO_DATA_FOUND extends PlcsqlRuntimeError {
        public NO_DATA_FOUND() {
            super(CODE_NO_DATA_FOUND, MSG_NO_DATA_FOUND);
        }
    }

    public static class PROGRAM_ERROR extends PlcsqlRuntimeError {
        public PROGRAM_ERROR() {
            super(CODE_PROGRAM_ERROR, MSG_PROGRAM_ERROR);
        }
    }

    public static class STORAGE_ERROR extends PlcsqlRuntimeError {
        public STORAGE_ERROR() {
            super(CODE_STORAGE_ERROR, MSG_STORAGE_ERROR);
        }
    }

    public static class SQL_ERROR extends PlcsqlRuntimeError {
        public SQL_ERROR(String s) {
            super(CODE_STORAGE_ERROR, (s == null || s.length() == 0) ? MSG_SQL_ERROR : s);
        }
    }

    public static class TOO_MANY_ROWS extends PlcsqlRuntimeError {
        public TOO_MANY_ROWS() {
            super(CODE_TOO_MANY_ROWS, MSG_TOO_MANY_ROWS);
        }
    }

    public static class VALUE_ERROR extends PlcsqlRuntimeError {
        public VALUE_ERROR() {
            super(CODE_VALUE_ERROR, MSG_VALUE_ERROR);
        }
    }

    public static class ZERO_DIVIDE extends PlcsqlRuntimeError {
        public ZERO_DIVIDE() {
            super(CODE_ZERO_DIVIDE, MSG_ZERO_DIVIDE);
        }
    }

    // user defined exception
    public static class $APP_ERROR extends PlcsqlRuntimeError {
        public $APP_ERROR(int code, String msg) {
            super(code, msg);
        }

        public $APP_ERROR(String msg) {
            super(CODE_APP_ERROR, msg);
        }

        public $APP_ERROR() {
            super(CODE_APP_ERROR, "a user defined exception");
        }
    }

    public static String SQLERRM = null;
    public static Integer SQLCODE = null;
    public static Date SYSDATE = null;

    public static void PUT_LINE(String s) {
        DBMS_OUTPUT.putLine(s);
    }

    public static class Query {
        public final String query;
        public ResultSet rs;

        public Query(String query) {
            this.query = query;
        }

        public void open(Connection conn, Object... val) {
            try {
                if (isOpen()) {
                    throw new CURSOR_ALREADY_OPEN();
                }
                PreparedStatement pstmt = conn.prepareStatement(query);
                for (int i = 0; i < val.length; i++) {
                    pstmt.setObject(i + 1, val[i]);
                }
                rs = pstmt.executeQuery();
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }

        public void close() {
            try {
                if (!isOpen()) {
                    throw new INVALID_CURSOR();
                }
                if (rs != null) {
                    Statement stmt = rs.getStatement();
                    if (stmt != null) {
                        stmt.close();
                    }
                    rs = null;
                }
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }

        public boolean isOpen() {
            try {
                return (rs != null && !rs.isClosed());
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }

        public boolean found() {
            try {
                if (!isOpen()) {
                    throw new INVALID_CURSOR();
                }
                return rs.getRow() > 0;
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }

        public boolean notFound() {
            try {
                if (!isOpen()) {
                    throw new INVALID_CURSOR();
                }
                return rs.getRow() == 0;
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }

        public long rowCount() {
            try {
                if (!isOpen()) {
                    throw new INVALID_CURSOR();
                }
                return (long) rs.getRow();
            } catch (SQLException e) {
                Server.log(e);
                throw new SQL_ERROR(e.getMessage());
            }
        }
    }

    // ------------------------------------
    // operators
    // ------------------------------------

    // ====================================
    // boolean not
    @Operator(coercionScheme = CoercionScheme.LogicalOp)
    public static Boolean opNot(Boolean l) {
        if (l == null) {
            return null;
        }
        return !l;
    }

    // ====================================
    // is null
    @Operator(coercionScheme = CoercionScheme.ObjectOp)
    public static Boolean opIsNull(Object l) {
        return (l == null);
    }

    // ====================================
    // arithmetic negative
    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Short opNeg(Short l) {
        if (l == null) {
            return null;
        }
        return ((short) -l);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Integer opNeg(Integer l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opNeg(Long l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static BigDecimal opNeg(BigDecimal l) {
        if (l == null) {
            return null;
        }
        return l.negate();
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Float opNeg(Float l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Double opNeg(Double l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Object opNeg(Object l) {
        if (l == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // bitwise compliment
    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Long opBitCompli(Short l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Long opBitCompli(Integer l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Long opBitCompli(Long l) {
        if (l == null) {
            return null;
        }
        return ~l;
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Object opBitCompli(Object l) {
        if (l == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // boolean and
    @Operator(coercionScheme = CoercionScheme.LogicalOp)
    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    // ====================================
    // boolean or
    @Operator(coercionScheme = CoercionScheme.LogicalOp)
    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    // ====================================
    // boolean xor
    @Operator(coercionScheme = CoercionScheme.LogicalOp)
    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    // ====================================
    // comparison equal

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Boolean l, Boolean r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(String l, String r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(BigDecimal l, BigDecimal r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Short l, Short r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Integer l, Integer r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Long l, Long r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Float l, Float r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Double l, Double r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Time l, Time r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Date l, Date r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Timestamp l, Timestamp r) {
        return commonOpEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return compareWithRuntimeTypeConv(l, r) == 0;
    }

    // ====================================
    // comparison null safe equal

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Boolean l, Boolean r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(String l, String r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(BigDecimal l, BigDecimal r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Short l, Short r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Integer l, Integer r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Long l, Long r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Float l, Float r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Double l, Double r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Time l, Time r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Date l, Date r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Timestamp l, Timestamp r) {
        return commonOpNullSafeEq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNullSafeEq(Object l, Object r) {
        if (l == null) {
            return (r == null);
        } else if (r == null) {
            return false;
        }

        return compareWithRuntimeTypeConv(l, r) == 0;
    }

    // ====================================
    // comparison not equal

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Boolean l, Boolean r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(String l, String r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(BigDecimal l, BigDecimal r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Short l, Short r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Integer l, Integer r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Long l, Long r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Float l, Float r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Double l, Double r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Time l, Time r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Date l, Date r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Timestamp l, Timestamp r) {
        return commonOpNeq(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }

        return compareWithRuntimeTypeConv(l, r) != 0;
    }

    // ====================================
    // comparison less than or equal to (<=)

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Boolean l, Boolean r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(String l, String r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Short l, Short r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Integer l, Integer r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Long l, Long r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(BigDecimal l, BigDecimal r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Float l, Float r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Double l, Double r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Date l, Date r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Time l, Time r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Timestamp l, Timestamp r) {
        return commonOpLe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLe(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return compareWithRuntimeTypeConv(l, r) <= 0;
    }

    // ====================================
    // comparison greater than or equal to (>=)
    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Boolean l, Boolean r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(String l, String r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Short l, Short r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Integer l, Integer r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Long l, Long r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(BigDecimal l, BigDecimal r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Float l, Float r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Double l, Double r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Date l, Date r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Time l, Time r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Timestamp l, Timestamp r) {
        return commonOpGe(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGe(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return compareWithRuntimeTypeConv(l, r) >= 0;
    }

    // ====================================
    // comparison less than (<)
    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Boolean l, Boolean r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(String l, String r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Short l, Short r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Integer l, Integer r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Long l, Long r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(BigDecimal l, BigDecimal r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Float l, Float r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Double l, Double r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Date l, Date r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Time l, Time r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Timestamp l, Timestamp r) {
        return commonOpLt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opLt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }

        return compareWithRuntimeTypeConv(l, r) < 0;
    }

    // ====================================
    // comparison greater than (>)
    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Boolean l, Boolean r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(String l, String r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Short l, Short r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Integer l, Integer r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Long l, Long r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(BigDecimal l, BigDecimal r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Float l, Float r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Double l, Double r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Date l, Date r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Time l, Time r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Timestamp l, Timestamp r) {
        return commonOpGt(l, r);
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.CompOp)
    public static Boolean opGt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }

        return compareWithRuntimeTypeConv(l, r) > 0;
    }

    // ====================================
    // between
    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Boolean o, Boolean lower, Boolean upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(String o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Short o, Short lower, Short upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Integer o, Integer lower, Integer upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Long o, Long lower, Long upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(BigDecimal o, BigDecimal lower, BigDecimal upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Float o, Float lower, Float upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Double o, Double lower, Double upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Date o, Date lower, Date upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Time o, Time lower, Time upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Timestamp o, Timestamp lower, Timestamp upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(ZonedDateTime o, ZonedDateTime lower, ZonedDateTime upper) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opBetween(Object o, Object lower, Object upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }

        return compareWithRuntimeTypeConv(lower, o) <= 0
                && compareWithRuntimeTypeConv(o, upper) <= 0;
    }

    // ====================================
    // in
    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Boolean o, Boolean... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(String o, String... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(BigDecimal o, BigDecimal... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Short o, Short... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Integer o, Integer... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Long o, Long... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Float o, Float... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Double o, Double... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Date o, Date... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Time o, Time... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Timestamp o, Timestamp... arr) {
        return commonOpIn(o, (Object[]) arr);
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(ZonedDateTime o, ZonedDateTime... arr) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.NAryCompOp)
    public static Boolean opIn(Object o, Object... arr) {
        assert arr != null; // guaranteed by the syntax
        if (o == null) {
            return null;
        }
        boolean nullFound = false;
        for (Object p : arr) {
            if (p == null) {
                nullFound = true;
            } else {
                if (compareWithRuntimeTypeConv(o, p) == 0) {
                    return true;
                }
            }
        }
        return nullFound ? null : false;
    }
    // ====================================
    // *
    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Short opMult(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l * r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Integer opMult(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opMult(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static BigDecimal opMult(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.multiply(r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Float opMult(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Double opMult(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Object opMult(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // /
    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Short opDiv(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals((short) 0)) {
            throw new ZERO_DIVIDE();
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Integer opDiv(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opDiv(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static BigDecimal opDiv(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(BigDecimal.ZERO)) {
            throw new ZERO_DIVIDE();
        }
        return l.divide(r, BigDecimal.ROUND_HALF_UP);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Float opDiv(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0.0f)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Double opDiv(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0.0)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Object opDiv(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // DIV
    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Short opDivInt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals((short) 0)) {
            throw new ZERO_DIVIDE();
        }
        return (short) (l / r);
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Integer opDivInt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Long opDivInt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l / r;
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Object opDivInt(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // MOD
    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Short opMod(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals((short) 0)) {
            throw new ZERO_DIVIDE();
        }
        return (short) (l % r);
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Integer opMod(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l % r;
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Long opMod(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        if (r.equals(0)) {
            throw new ZERO_DIVIDE();
        }
        return l % r;
    }

    @Operator(coercionScheme = CoercionScheme.IntArithOp)
    public static Object opMod(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // +
    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Short opAdd(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l + r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Integer opAdd(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opAdd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static BigDecimal opAdd(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.add(r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Float opAdd(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Double opAdd(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Time opAdd(Time l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.plusSeconds(r.longValue()));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Time opAdd(Long l, Time r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Date opAdd(Date l, Long r) {
        if (l == null || r == null) {
            return null;
        }

        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.plusDays(r.longValue()));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Date opAdd(Long l, Date r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Timestamp opAdd(Timestamp l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.plus(r.longValue(), ChronoUnit.MILLIS));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Timestamp opAdd(Long l, Timestamp r) {
        return opAdd(r, l);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static ZonedDateTime opAdd(ZonedDateTime l, Long r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static ZonedDateTime opAdd(Long l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Object opAdd(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // -
    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Short opSubtract(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l - r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Integer opSubtract(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opSubtract(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static BigDecimal opSubtract(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.subtract(r);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Float opSubtract(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Double opSubtract(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opSubtract(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        LocalTime rlt = r.toLocalTime();
        return rlt.until(llt, ChronoUnit.SECONDS);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opSubtract(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        LocalDate rld = r.toLocalDate();
        return rld.until(lld, ChronoUnit.DAYS);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opSubtract(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        LocalDateTime rldt = r.toLocalDateTime();
        return rldt.until(lldt, ChronoUnit.MILLIS);
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Long opSubtract(ZonedDateTime l, ZonedDateTime r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Time opSubtract(Time l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalTime llt = l.toLocalTime();
        return Time.valueOf(llt.minusSeconds(r.longValue()));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Date opSubtract(Date l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDate lld = l.toLocalDate();
        return Date.valueOf(lld.minusDays(r.longValue()));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Timestamp opSubtract(Timestamp l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        LocalDateTime lldt = l.toLocalDateTime();
        return Timestamp.valueOf(lldt.minus(r.longValue(), ChronoUnit.MILLIS));
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static ZonedDateTime opSubtract(ZonedDateTime l, Long r) {
        // cannot be called actually, but only to register this operator with a parameter type
        // TIMESTAMP
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    @Operator(coercionScheme = CoercionScheme.ArithOp)
    public static Object opSubtract(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        assert false : "unreachable";
        throw new PROGRAM_ERROR(); // unreachable
    }

    // ====================================
    // ||
    @Operator(coercionScheme = CoercionScheme.StringOp)
    public static String opConcat(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    // ====================================
    // <<
    @Operator(coercionScheme = CoercionScheme.BitOp)
    public static Long opBitShiftLeft(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l << r;
    }

    // ====================================
    // >>
    @Operator(coercionScheme = CoercionScheme.BitOp)
    public static Long opBitShiftRight(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l >> r;
    }

    // ====================================
    // &
    @Operator(coercionScheme = CoercionScheme.BitOp)
    public static Long opBitAnd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l & r;
    }

    // ====================================
    // ^
    @Operator(coercionScheme = CoercionScheme.BitOp)
    public static Long opBitXor(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l ^ r;
    }

    // ====================================
    // |
    @Operator(coercionScheme = CoercionScheme.BitOp)
    public static Long opBitOr(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l | r;
    }

    // ====================================
    // like
    @Operator(coercionScheme = CoercionScheme.StringOp)
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
            throw new PROGRAM_ERROR(); // unreachable
        }
    }

    // ------------------------------------
    // coercions
    // ------------------------------------

    // from datetime
    public static Date convDatetimeToDate(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Date(e.getYear(), e.getMonth(), e.getDate());
    }

    public static Time convDatetimeToTime(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Time(e.getHours(), e.getMinutes(), e.getSeconds());
    }

    public static Timestamp convDatetimeToTimestamp(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Timestamp(
                e.getYear(),
                e.getMonth(),
                e.getDate(),
                e.getHours(),
                e.getMinutes(),
                e.getSeconds(),
                0);
    }

    public static String convDatetimeToString(Timestamp e) {
        if (e == null) {
            return null;
        }

        return datetimeFormat.format(e);
    }

    // from date
    public static Timestamp convDateToDatetime(Date e) {
        if (e == null) {
            return null;
        }

        return new Timestamp(e.getYear(), e.getMonth(), e.getDate(), 0, 0, 0, 0);
    }

    public static Timestamp convDateToTimestamp(Date e) {
        if (e == null) {
            return null;
        }

        return new Timestamp(e.getYear(), e.getMonth(), e.getDate(), 0, 0, 0, 0);
    }

    public static String convDateToString(Date e) {
        if (e == null) {
            return null;
        }

        return dateFormat.format(e);
    }

    // from time
    public static String convTimeToString(Time e) {
        if (e == null) {
            return null;
        }

        return timeFormat.format(e);
    }

    // from timestamp
    public static Timestamp convTimestampToDatetime(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Timestamp(
                e.getYear(),
                e.getMonth(),
                e.getDate(),
                e.getHours(),
                e.getMinutes(),
                e.getSeconds(),
                0);
    }

    public static Date convTimestampToDate(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Date(e.getYear(), e.getMonth(), e.getDate());
    }

    public static Time convTimestampToTime(Timestamp e) {
        if (e == null) {
            return null;
        }

        return new Time(e.getHours(), e.getMinutes(), e.getSeconds());
    }

    public static String convTimestampToString(Timestamp e) {
        if (e == null) {
            return null;
        }

        return timestampFormat.format(e);
    }

    // from double
    public static Time convDoubleToTime(Double e) {
        if (e == null) {
            return null;
        }

        long l = doubleToLong(e.doubleValue());
        return longToTime(l);
    }

    public static Timestamp convDoubleToTimestamp(Double e) {
        if (e == null) {
            return null;
        }

        long l = doubleToLong(e.doubleValue());
        return longToTimestamp(l);
    }

    public static Integer convDoubleToInt(Double e) {
        if (e == null) {
            return null;
        }

        return Integer.valueOf(doubleToInt(e.doubleValue()));
    }

    public static Short convDoubleToShort(Double e) {
        if (e == null) {
            return null;
        }

        return Short.valueOf(doubleToShort(e.doubleValue()));
    }

    public static String convDoubleToString(Double e) {
        if (e == null) {
            return null;
        }

        return e.toString();
    }

    public static Float convDoubleToFloat(Double e) {
        if (e == null) {
            return null;
        }

        return Float.valueOf(e.floatValue());
    }

    public static BigDecimal convDoubleToNumeric(Double e) {
        if (e == null) {
            return null;
        }

        return BigDecimal.valueOf(e.doubleValue());
    }

    public static Long convDoubleToBigint(Double e) {
        if (e == null) {
            return null;
        }

        return Long.valueOf(doubleToLong(e.doubleValue()));
    }

    // from float
    public static Time convFloatToTime(Float e) {
        if (e == null) {
            return null;
        }

        long l = doubleToLong(e.doubleValue());
        return longToTime(l);
    }

    public static Timestamp convFloatToTimestamp(Float e) {
        if (e == null) {
            return null;
        }

        long l = doubleToLong(e.doubleValue());
        return longToTimestamp(l);
    }

    public static Integer convFloatToInt(Float e) {
        if (e == null) {
            return null;
        }

        return Integer.valueOf(doubleToInt(e.doubleValue()));
    }

    public static Short convFloatToShort(Float e) {
        if (e == null) {
            return null;
        }

        return Short.valueOf(doubleToShort(e.doubleValue()));
    }

    public static String convFloatToString(Float e) {
        if (e == null) {
            return null;
        }

        return e.toString();
    }

    public static Double convFloatToDouble(Float e) {
        if (e == null) {
            return null;
        }

        return Double.valueOf(e.doubleValue());
    }

    public static BigDecimal convFloatToNumeric(Float e) {
        if (e == null) {
            return null;
        }

        return BigDecimal.valueOf(e.doubleValue());
    }

    public static Long convFloatToBigint(Float e) {
        if (e == null) {
            return null;
        }

        return Long.valueOf(doubleToLong(e.doubleValue()));
    }

    // from numeric
    public static Timestamp convNumericToTimestamp(BigDecimal e) {
        if (e == null) {
            return null;
        }

        long l = bigDecimalToLong(e);
        return longToTimestamp(l);
    }

    public static Integer convNumericToInt(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return Integer.valueOf(bigDecimalToInt(e));
    }

    public static Short convNumericToShort(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return Short.valueOf(bigDecimalToShort(e));
    }

    public static String convNumericToString(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return e.toPlainString();
    }

    public static Double convNumericToDouble(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return Double.valueOf(e.doubleValue());
    }

    public static Float convNumericToFloat(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return Float.valueOf(e.floatValue());
    }

    public static Long convNumericToBigint(BigDecimal e) {
        if (e == null) {
            return null;
        }

        return Long.valueOf(bigDecimalToLong(e));
    }

    // from bigint
    public static Time convBigintToTime(Long e) {
        if (e == null) {
            return null;
        }

        return longToTime(e.longValue());
    }

    public static Timestamp convBigintToTimestamp(Long e) {
        if (e == null) {
            return null;
        }

        return longToTimestamp(e.longValue());
    }

    public static Integer convBigintToInt(Long e) {
        if (e == null) {
            return null;
        }

        return Integer.valueOf(longToInt(e.longValue()));
    }

    public static Short convBigintToShort(Long e) {
        if (e == null) {
            return null;
        }

        return Short.valueOf(longToShort(e.longValue()));
    }

    public static String convBigintToString(Long e) {
        if (e == null) {
            return null;
        }

        return e.toString();
    }

    public static Double convBigintToDouble(Long e) {
        if (e == null) {
            return null;
        }

        return Double.valueOf(e.doubleValue());
    }

    public static Float convBigintToFloat(Long e) {
        if (e == null) {
            return null;
        }

        return Float.valueOf(e.floatValue());
    }

    public static BigDecimal convBigintToNumeric(Long e) {
        if (e == null) {
            return null;
        }

        return BigDecimal.valueOf(e.longValue());
    }

    // from int
    public static Time convIntToTime(Integer e) {
        if (e == null) {
            return null;
        }

        return longToTime(e.longValue());
    }

    public static Timestamp convIntToTimestamp(Integer e) {
        if (e == null) {
            return null;
        }

        return longToTimestamp(e.longValue());
    }

    public static Short convIntToShort(Integer e) {
        if (e == null) {
            return null;
        }

        return Short.valueOf(longToShort(e.longValue()));
    }

    public static String convIntToString(Integer e) {
        if (e == null) {
            return null;
        }

        return e.toString();
    }

    public static Double convIntToDouble(Integer e) {
        if (e == null) {
            return null;
        }

        return Double.valueOf(e.doubleValue());
    }

    public static Float convIntToFloat(Integer e) {
        if (e == null) {
            return null;
        }

        return Float.valueOf(e.floatValue());
    }

    public static BigDecimal convIntToNumeric(Integer e) {
        if (e == null) {
            return null;
        }

        return BigDecimal.valueOf(e.longValue());
    }

    public static Long convIntToBigint(Integer e) {
        if (e == null) {
            return null;
        }

        return Long.valueOf(e.longValue());
    }

    // from short
    public static Time convShortToTime(Short e) {
        if (e == null) {
            return null;
        }

        return longToTime(e.longValue());
    }

    public static Timestamp convShortToTimestamp(Short e) {
        if (e == null) {
            return null;
        }

        return longToTimestamp(e.longValue());
    }

    public static Integer convShortToInt(Short e) {
        if (e == null) {
            return null;
        }

        return Integer.valueOf(e.intValue());
    }

    public static String convShortToString(Short e) {
        if (e == null) {
            return null;
        }

        return e.toString();
    }

    public static Double convShortToDouble(Short e) {
        if (e == null) {
            return null;
        }

        return Double.valueOf(e.doubleValue());
    }

    public static Float convShortToFloat(Short e) {
        if (e == null) {
            return null;
        }

        return Float.valueOf(e.floatValue());
    }

    public static BigDecimal convShortToNumeric(Short e) {
        if (e == null) {
            return null;
        }

        return BigDecimal.valueOf(e.longValue());
    }

    public static Long convShortToBigint(Short e) {
        if (e == null) {
            return null;
        }

        return Long.valueOf(e.longValue());
    }

    // from string
    public static Timestamp convStringToDatetime(String e) {
        if (e == null) {
            return null;
        }

        LocalDateTime dt = DateTimeParser.DatetimeLiteral.parse(e);
        if (dt == null) {
            // invalid string
            throw new VALUE_ERROR();
        }

        if (dt.equals(DateTimeParser.nullDatetime)) {
            return new Timestamp(-1900, -1, 0, 0, 0, 0, 0);
        } else {
            return new Timestamp(
                    dt.getYear() - 1900,
                    dt.getMonthValue() - 1,
                    dt.getDayOfMonth(),
                    dt.getHour(),
                    dt.getMinute(),
                    dt.getSecond(),
                    dt.getNano());
        }
    }

    public static Date convStringToDate(String e) {
        if (e == null) {
            return null;
        }

        LocalDate d = DateTimeParser.DateLiteral.parse(e);
        if (d == null) {
            // invalid string
            throw new VALUE_ERROR();
        }

        if (d.equals(DateTimeParser.nullDate)) {
            return new Date(-1900, -1, 0);
        } else {
            return new Date(d.getYear() - 1900, d.getMonthValue() - 1, d.getDayOfMonth());
        }
    }

    public static Time convStringToTime(String e) {
        if (e == null) {
            return null;
        }

        LocalTime t = DateTimeParser.TimeLiteral.parse(e);
        if (t == null) {
            // invalid string
            throw new VALUE_ERROR();
        }

        return new Time(t.getHour(), t.getMinute(), t.getSecond());
    }

    public static Timestamp convStringToTimestamp(String e) {
        if (e == null) {
            return null;
        }

        ZonedDateTime zdt = DateTimeParser.TimestampLiteral.parse(e);
        if (zdt == null) {
            // invalid string
            throw new VALUE_ERROR();
        }

        if (zdt.equals(DateTimeParser.nullDatetimeUTC)) {
            return new Timestamp(-1900, -1, 0, 0, 0, 0, 0);
        } else {
            assert zdt.getNano() == 0;
            return new Timestamp(
                    zdt.getYear() - 1900,
                    zdt.getMonthValue() - 1,
                    zdt.getDayOfMonth(),
                    zdt.getHour(),
                    zdt.getMinute(),
                    zdt.getSecond(),
                    0);
        }
    }

    public static Integer convStringToInt(String e) {
        if (e == null) {
            return null;
        }

        if (e.length() == 0) {
            return INT_ZERO;
        }

        BigDecimal bd = strToBigDecimal(e);
        ;
        return Integer.valueOf(bigDecimalToInt(bd));
    }

    public static Short convStringToShort(String e) {
        if (e == null) {
            return null;
        }

        if (e.length() == 0) {
            return SHORT_ZERO;
        }

        BigDecimal bd = strToBigDecimal(e);
        ;
        return Short.valueOf(bigDecimalToShort(bd));
    }

    public static Double convStringToDouble(String e) {
        if (e == null) {
            return null;
        }

        if (e.length() == 0) {
            return DOUBLE_ZERO;
        }

        try {
            return Double.valueOf(e);
        } catch (NumberFormatException ex) {
            throw new VALUE_ERROR();
        }
    }

    public static Float convStringToFloat(String e) {
        if (e == null) {
            return null;
        }

        if (e.length() == 0) {
            return FLOAT_ZERO;
        }

        try {
            return Float.valueOf(e);
        } catch (NumberFormatException ex) {
            throw new VALUE_ERROR();
        }
    }

    public static BigDecimal convStringToNumeric(String e) {
        if (e == null || e.length() == 0) {
            return null;
        }

        return strToBigDecimal(e);
    }

    public static Long convStringToBigint(String e) {
        if (e == null) {
            return null;
        }

        if (e.length() == 0) {
            return LONG_ZERO;
        }

        BigDecimal bd = strToBigDecimal(e);
        ;
        return Long.valueOf(bigDecimalToLong(bd));
    }

    // from Object
    public static Timestamp convObjectToDatetime(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToDatetime((String) e);
        } else if (e instanceof Date) {
            return convDateToDatetime((Date) e);
        } else if (e instanceof Timestamp) {
            // e is DATETIME or TIMESTAMP
            return (Timestamp) e;
        }

        throw new VALUE_ERROR();
    }

    public static Date convObjectToDate(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToDate((String) e);
        } else if (e instanceof Date) {
            return (Date) e;
        } else if (e instanceof Timestamp) {
            // e is DATETIME or TIMESTAMP
            return convTimestampToDate((Timestamp) e);
        }

        throw new VALUE_ERROR();
    }

    public static Time convObjectToTime(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToTime((String) e);
        } else if (e instanceof Short) {
            return convShortToTime((Short) e);
        } else if (e instanceof Integer) {
            return convIntToTime((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToTime((Long) e);
        } else if (e instanceof Float) {
            return convFloatToTime((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToTime((Double) e);
        } else if (e instanceof Time) {
            return (Time) e;
        } else if (e instanceof Timestamp) {
            // e is DATETIME or TIMESTAMP
            return convTimestampToTime((Timestamp) e);
        }

        throw new VALUE_ERROR();
    }

    public static Timestamp convObjectToTimestamp(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToTimestamp((String) e);
        } else if (e instanceof Short) {
            return convShortToTimestamp((Short) e);
        } else if (e instanceof Integer) {
            return convIntToTimestamp((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToTimestamp((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToTimestamp((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToTimestamp((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToTimestamp((Double) e);
        } else if (e instanceof Date) {
            return convDateToTimestamp((Date) e);
        } else if (e instanceof Timestamp) {
            // e is DATETIME or TIMESTAMP
            return convDatetimeToTimestamp((Timestamp) e);
        }

        throw new VALUE_ERROR();
    }

    public static Integer convObjectToInt(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToInt((String) e);
        } else if (e instanceof Short) {
            return convShortToInt((Short) e);
        } else if (e instanceof Integer) {
            return (Integer) e;
        } else if (e instanceof Long) {
            return convBigintToInt((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToInt((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToInt((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToInt((Double) e);
        }

        throw new VALUE_ERROR();
    }

    public static Short convObjectToShort(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToShort((String) e);
        } else if (e instanceof Short) {
            return (Short) e;
        } else if (e instanceof Integer) {
            return convIntToShort((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToShort((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToShort((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToShort((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToShort((Double) e);
        }

        throw new VALUE_ERROR();
    }

    public static String convObjectToString(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return (String) e;
        } else if (e instanceof Short) {
            return convShortToString((Short) e);
        } else if (e instanceof Integer) {
            return convIntToString((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToString((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToString((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToString((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToString((Double) e);
        } else if (e instanceof Date) {
            return convDateToString((Date) e);
        } else if (e instanceof Time) {
            return convTimeToString((Time) e);
            /*
            } else if (e instanceof Timestamp) {
                // e is DATETIME or TIMESTAMP. impossible to figure out for now
                // TODO: match different Java types to DATETIME and TIMESTAMP, respectively
            */
        }

        throw new VALUE_ERROR();
    }

    public static Double convObjectToDouble(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToDouble((String) e);
        } else if (e instanceof Short) {
            return convShortToDouble((Short) e);
        } else if (e instanceof Integer) {
            return convIntToDouble((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToDouble((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToDouble((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToDouble((Float) e);
        } else if (e instanceof Double) {
            return (Double) e;
        }

        throw new VALUE_ERROR();
    }

    public static Float convObjectToFloat(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToFloat((String) e);
        } else if (e instanceof Short) {
            return convShortToFloat((Short) e);
        } else if (e instanceof Integer) {
            return convIntToFloat((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToFloat((Long) e);
        } else if (e instanceof BigDecimal) {
            return convNumericToFloat((BigDecimal) e);
        } else if (e instanceof Float) {
            return (Float) e;
        } else if (e instanceof Double) {
            return convDoubleToFloat((Double) e);
        }

        throw new VALUE_ERROR();
    }

    public static BigDecimal convObjectToNumeric(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToNumeric((String) e);
        } else if (e instanceof Short) {
            return convShortToNumeric((Short) e);
        } else if (e instanceof Integer) {
            return convIntToNumeric((Integer) e);
        } else if (e instanceof Long) {
            return convBigintToNumeric((Long) e);
        } else if (e instanceof BigDecimal) {
            return (BigDecimal) e;
        } else if (e instanceof Float) {
            return convFloatToNumeric((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToNumeric((Double) e);
        }

        throw new VALUE_ERROR();
    }

    public static Long convObjectToBigint(Object e) {
        if (e == null) {
            return null;
        }

        if (e instanceof String) {
            return convStringToBigint((String) e);
        } else if (e instanceof Short) {
            return convShortToBigint((Short) e);
        } else if (e instanceof Integer) {
            return convIntToBigint((Integer) e);
        } else if (e instanceof Long) {
            return (Long) e;
        } else if (e instanceof BigDecimal) {
            return convNumericToBigint((BigDecimal) e);
        } else if (e instanceof Float) {
            return convFloatToBigint((Float) e);
        } else if (e instanceof Double) {
            return convDoubleToBigint((Double) e);
        }

        throw new VALUE_ERROR();
    }

    // ------------------------------------------------
    // Private
    // ------------------------------------------------

    private static final int CODE_CASE_NOT_FOUND = 0;
    private static final int CODE_CURSOR_ALREADY_OPEN = 1;
    private static final int CODE_INVALID_CURSOR = 2;
    private static final int CODE_NO_DATA_FOUND = 3;
    private static final int CODE_PROGRAM_ERROR = 4;
    private static final int CODE_STORAGE_ERROR = 5;
    private static final int CODE_SQL_ERROR = 6;
    private static final int CODE_TOO_MANY_ROWS = 7;
    private static final int CODE_VALUE_ERROR = 8;
    private static final int CODE_ZERO_DIVIDE = 9;

    private static final int CODE_APP_ERROR = 99;

    private static final String MSG_CASE_NOT_FOUND = "case not found";
    private static final String MSG_CURSOR_ALREADY_OPEN = "cursor already open";
    private static final String MSG_INVALID_CURSOR = "invalid cursor";
    private static final String MSG_NO_DATA_FOUND = "no data found";
    private static final String MSG_PROGRAM_ERROR = "internal server error";
    private static final String MSG_STORAGE_ERROR = "storage error";
    private static final String MSG_SQL_ERROR = "error while running SQL";
    private static final String MSG_TOO_MANY_ROWS = "too many rows";
    private static final String MSG_VALUE_ERROR = "value error";
    private static final String MSG_ZERO_DIVIDE = "division by zero";

    private static final Short SHORT_ZERO = Short.valueOf((short) 0);
    private static final Integer INT_ZERO = Integer.valueOf(0);
    private static final Long LONG_ZERO = Long.valueOf(0L);
    private static final Float FLOAT_ZERO = Float.valueOf(0.0f);
    private static final Double DOUBLE_ZERO = Double.valueOf(0.0);

    private static final DateFormat dateFormat = new SimpleDateFormat("MM/dd/yyyy");
    private static final DateFormat timeFormat = new SimpleDateFormat("hh:mm:ss a");
    private static final DateFormat datetimeFormat =
            new SimpleDateFormat("hh:mm:ss.SSS a MM/dd/yyyy");
    private static final DateFormat timestampFormat = new SimpleDateFormat("hh:mm:ss a MM/dd/yyyy");

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

    private static Boolean commonOpIn(Object o, Object... arr) {
        assert arr != null; // guaranteed by the syntax
        if (o == null) {
            return null;
        }
        boolean nullFound = false;
        for (Object p : arr) {
            if (p == null) {
                nullFound = true;
            } else {
                if (o.equals(p)) {
                    return true;
                }
            }
        }
        return nullFound ? null : false;
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

    private static long doubleToLong(double d) {
        BigDecimal bd = BigDecimal.valueOf(d);
        return bigDecimalToLong(bd);
    }

    private static int doubleToInt(double d) {
        BigDecimal bd = BigDecimal.valueOf(d);
        return bigDecimalToInt(bd);
    }

    private static short doubleToShort(double d) {
        BigDecimal bd = BigDecimal.valueOf(d);
        return bigDecimalToShort(bd);
    }

    private static long bigDecimalToLong(BigDecimal bd) {
        bd = bd.setScale(0, RoundingMode.HALF_UP); // 1.5 -->2, and -1.5 --> -2 NOTE: different from
        // Math.round
        try {
            return bd.longValueExact();
        } catch (ArithmeticException e) {
            throw new VALUE_ERROR();
        }
    }

    private static int bigDecimalToInt(BigDecimal bd) {
        bd = bd.setScale(0, RoundingMode.HALF_UP); // 1.5 -->2, and -1.5 --> -2 NOTE: different from
        // Math.round
        try {
            return bd.intValueExact();
        } catch (ArithmeticException e) {
            throw new VALUE_ERROR();
        }
    }

    private static short bigDecimalToShort(BigDecimal bd) {
        bd = bd.setScale(0, RoundingMode.HALF_UP); // 1.5 -->2, and -1.5 --> -2 NOTE: different from
        // Math.round
        try {
            return bd.shortValueExact();
        } catch (ArithmeticException e) {
            throw new VALUE_ERROR();
        }
    }

    private static Time longToTime(long l) {
        if (l < 0L) {
            // negative values seem to result in a invalid time value
            // e.g.
            // select cast(cast(-1 as bigint) as time);
            // === <Result of SELECT Command in Line 1> ===
            //
            // <00001>  cast( cast(-1 as bigint) as time): 12:00:0/ AM
            //
            // 1 row selected. (0.004910 sec) Committed. (0.000020 sec)
            throw new VALUE_ERROR();
        }

        int totalSec = (int) (l % 86400L);
        int hour = totalSec / 3600;
        int minuteSec = totalSec % 3600;
        int min = minuteSec / 60;
        int sec = minuteSec % 60;
        return new Time(hour, min, sec);
    }

    private static Timestamp longToTimestamp(long l) {
        if (l < 0L) {
            //   select cast(cast(-100 as bigint) as timestamp);
            //   ERROR: Cannot coerce value of domain "bigint" to domain "timestamp"
            throw new VALUE_ERROR();
        } else if (l
                > 2147483647L) { // 2147483647L : see section 'implicit type conversion' in the user
            // manual
            throw new VALUE_ERROR();
        } else {
            return new Timestamp(l * 1000L); // * 1000 : converts it to milli-seconds
        }
    }

    private static int longToInt(long l) {
        return bigDecimalToInt(BigDecimal.valueOf(l));
    }

    private static short longToShort(long l) {
        return bigDecimalToShort(BigDecimal.valueOf(l));
    }

    private static BigDecimal strToBigDecimal(String s) {
        try {
            return new BigDecimal(s);
        } catch (NumberFormatException e) {
            throw new VALUE_ERROR();
        }
    }

    private static int compareWithRuntimeTypeConv(Object l, Object r) {
        assert l != null;
        assert r != null;

        Comparable lConv = null;
        Comparable rConv = null;

        if (l instanceof Boolean) {
            if (r instanceof Boolean) {
                lConv = (Boolean) l;
                rConv = (Boolean) r;
            } else if (r instanceof String) {
                // not applicable
            } else if (r instanceof Short) {
                // not applicable
            } else if (r instanceof Integer) {
                // not applicable
            } else if (r instanceof Long) {
                // not applicable
            } else if (r instanceof BigDecimal) {
                // not applicable
            } else if (r instanceof Float) {
                // not applicable
            } else if (r instanceof Double) {
                // not applicable
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof String) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = (String) l;
                rConv = (String) r;
            } else if (r instanceof Short) {
                lConv = convStringToDouble((String) l);
                rConv = convShortToDouble((Short) r);
            } else if (r instanceof Integer) {
                lConv = convStringToDouble((String) l);
                rConv = convIntToDouble((Integer) r);
            } else if (r instanceof Long) {
                lConv = convStringToDouble((String) l);
                rConv = convBigintToDouble((Long) r);
            } else if (r instanceof BigDecimal) {
                lConv = convStringToDouble((String) l);
                rConv = convNumericToDouble((BigDecimal) r);
            } else if (r instanceof Float) {
                lConv = convStringToDouble((String) l);
                rConv = convFloatToDouble((Float) r);
            } else if (r instanceof Double) {
                lConv = convStringToDouble((String) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                lConv = convStringToDate((String) l);
                rConv = (Date) r;
            } else if (r instanceof Time) {
                lConv = convStringToTime((String) l);
                rConv = (Time) r;
            } else if (r instanceof Timestamp) {
                lConv = convStringToTimestamp((String) l);
                rConv = (Timestamp) r;
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Short) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = convShortToDouble((Short) l);
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (Short) (Short) l;
                rConv = (Short) r;
            } else if (r instanceof Integer) {
                lConv = convShortToInt((Short) l);
                rConv = (Integer) r;
            } else if (r instanceof Long) {
                lConv = convShortToBigint((Short) l);
                rConv = (Long) r;
            } else if (r instanceof BigDecimal) {
                lConv = convShortToNumeric((Short) l);
                rConv = (BigDecimal) r;
            } else if (r instanceof Float) {
                lConv = convShortToFloat((Short) l);
                rConv = (Float) r;
            } else if (r instanceof Double) {
                lConv = convShortToDouble((Short) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                lConv = convShortToTime((Short) l);
                rConv = (Time) r;
            } else if (r
                    instanceof Timestamp) { // NOTE: r cannot be a DATETIME by compile time check in
                // CoercionScheme
                lConv = convShortToTimestamp((Short) l);
                rConv = (Timestamp) r;
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Integer) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = convIntToDouble((Integer) l);
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (Integer) l;
                rConv = convShortToInt((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Integer) l;
                rConv = (Integer) r;
            } else if (r instanceof Long) {
                lConv = convIntToBigint((Integer) l);
                rConv = (Long) r;
            } else if (r instanceof BigDecimal) {
                lConv = convIntToNumeric((Integer) l);
                rConv = (BigDecimal) r;
            } else if (r instanceof Float) {
                lConv = convIntToFloat((Integer) l);
                rConv = (Float) r;
            } else if (r instanceof Double) {
                lConv = convIntToDouble((Integer) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                lConv = convIntToTime((Integer) l);
                rConv = (Time) r;
            } else if (r
                    instanceof Timestamp) { // NOTE: r cannot be a DATETIME by compile time check in
                // CoercionScheme
                lConv = convIntToTimestamp((Integer) l);
                rConv = (Timestamp) r;
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Long) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = convBigintToDouble((Long) l);
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (Long) l;
                rConv = convShortToBigint((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Long) l;
                rConv = convIntToBigint((Integer) r);
            } else if (r instanceof Long) {
                lConv = (Long) l;
                rConv = (Long) r;
            } else if (r instanceof BigDecimal) {
                lConv = convBigintToNumeric((Long) l);
                rConv = (BigDecimal) r;
            } else if (r instanceof Float) {
                lConv = convBigintToFloat((Long) l);
                rConv = (Float) r;
            } else if (r instanceof Double) {
                lConv = convBigintToDouble((Long) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                lConv = convBigintToTime((Long) l);
                rConv = (Time) r;
            } else if (r
                    instanceof Timestamp) { // NOTE: r cannot be a DATETIME by compile time check in
                // CoercionScheme
                lConv = convBigintToTimestamp((Long) l);
                rConv = (Timestamp) r;
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof BigDecimal) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = convNumericToDouble((BigDecimal) l);
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (BigDecimal) l;
                rConv = convShortToNumeric((Short) r);
            } else if (r instanceof Integer) {
                lConv = (BigDecimal) l;
                rConv = convIntToNumeric((Integer) r);
            } else if (r instanceof Long) {
                lConv = (BigDecimal) l;
                rConv = convBigintToNumeric((Long) r);
            } else if (r instanceof BigDecimal) {
                lConv = (BigDecimal) l;
                rConv = (BigDecimal) r;
            } else if (r instanceof Float) {
                lConv = convNumericToDouble((BigDecimal) l);
                rConv = convFloatToDouble((Float) r);
            } else if (r instanceof Double) {
                lConv = convNumericToDouble((BigDecimal) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
                // not applicable
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Float) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = convFloatToDouble((Float) l);
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (Float) l;
                rConv = convShortToFloat((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Float) l;
                rConv = convIntToFloat((Integer) r);
            } else if (r instanceof Long) {
                lConv = (Float) l;
                rConv = convBigintToFloat((Long) r);
            } else if (r instanceof BigDecimal) {
                lConv = convFloatToDouble((Float) l);
                rConv = convNumericToDouble((BigDecimal) r);
            } else if (r instanceof Float) {
                lConv = (Float) l;
                rConv = (Float) r;
            } else if (r instanceof Double) {
                lConv = convFloatToDouble((Float) l);
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
                // not applicable
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Double) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = (Double) l;
                rConv = convStringToDouble((String) r);
            } else if (r instanceof Short) {
                lConv = (Double) l;
                rConv = convShortToDouble((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Double) l;
                rConv = convIntToDouble((Integer) r);
            } else if (r instanceof Long) {
                lConv = (Double) l;
                rConv = convBigintToDouble((Long) r);
            } else if (r instanceof BigDecimal) {
                lConv = (Double) l;
                rConv = convNumericToDouble((BigDecimal) r);
            } else if (r instanceof Float) {
                lConv = (Double) l;
                rConv = convFloatToDouble((Float) r);
            } else if (r instanceof Double) {
                lConv = (Double) l;
                rConv = (Double) r;
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
                // not applicable
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Date) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = (Date) l;
                rConv = convStringToDate((String) r);
            } else if (r instanceof Short) {
                // not applicable
            } else if (r instanceof Integer) {
                // not applicable
            } else if (r instanceof Long) {
                // not applicable
            } else if (r instanceof BigDecimal) {
                // not applicable
            } else if (r instanceof Float) {
                // not applicable
            } else if (r instanceof Double) {
                // not applicable
            } else if (r instanceof Date) {
                lConv = (Date) l;
                rConv = (Date) r;
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
                // not applicable
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Time) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = (Time) l;
                rConv = convStringToTime((String) r);
            } else if (r instanceof Short) {
                lConv = (Time) l;
                rConv = convShortToTime((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Time) l;
                rConv = convIntToTime((Integer) r);
            } else if (r instanceof Long) {
                lConv = (Time) l;
                rConv = convBigintToTime((Long) r);
            } else if (r instanceof BigDecimal) {
                // not applicable
            } else if (r instanceof Float) {
                // not applicable
            } else if (r instanceof Double) {
                // not applicable
            } else if (r instanceof Date) {
                // not applicable
            } else if (r instanceof Time) {
                lConv = (Time) l;
                rConv = (Time) r;
            } else if (r instanceof Timestamp) {
                // not applicable
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else if (l instanceof Timestamp) {

            if (r instanceof Boolean) {
                // not applicable
            } else if (r instanceof String) {
                lConv = (Timestamp) l;
                rConv = convStringToTimestamp((String) r);
            } else if (r instanceof Short) {
                lConv = (Timestamp) l;
                rConv = convShortToTimestamp((Short) r);
            } else if (r instanceof Integer) {
                lConv = (Timestamp) l;
                rConv = convIntToTimestamp((Integer) r);
            } else if (r instanceof Long) {
                lConv = (Timestamp) l;
                rConv = convBigintToTimestamp((Long) r);
            } else if (r instanceof BigDecimal) {
                // not applicable
            } else if (r instanceof Float) {
                // not applicable
            } else if (r instanceof Double) {
                // not applicable
            } else if (r instanceof Date) {
                lConv = (Timestamp) l;
                rConv = convDateToTimestamp((Date) r);
            } else if (r instanceof Time) {
                // not applicable
            } else if (r instanceof Timestamp) {
                lConv = (Timestamp) l;
                rConv = (Timestamp) r;
            } else {
                assert false : "unreachable";
                throw new PROGRAM_ERROR(); // unreachable
            }

        } else {
            assert false : "unreachable";
            throw new PROGRAM_ERROR(); // unreachable
        }

        if (lConv == null) {
            assert rConv == null;
            throw new VALUE_ERROR(); // cannot compare two values of unsupported types
        } else {
            assert rConv != null;
            return lConv.compareTo(rConv);
        }
    }
}
