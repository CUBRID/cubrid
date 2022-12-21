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

import com.cubrid.plcsql.predefined.ZonedTimestamp;

import org.apache.commons.collections4.MultiSet;
import org.apache.commons.collections4.multiset.HashMultiSet;

import java.sql.*;

import java.time.LocalTime;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.ZonedDateTime;
import java.time.temporal.ChronoUnit;

import java.util.Set;
import java.util.HashSet;
import java.util.List;
import java.util.ArrayList;
import java.util.Date;
import java.util.Objects;

import java.math.BigDecimal;

public class SpLib {

    public static class $$APP_ERROR extends RuntimeException {}

    public static class $CASE_NOT_FOUND extends RuntimeException {}

    public static class $CURSOR_ALREADY_OPEN extends RuntimeException {}

    public static class $DUP_VAL_ON_INDEX extends RuntimeException {}

    public static class $INVALID_CURSOR extends RuntimeException {}

    public static class $LOGIN_DENIED extends RuntimeException {}

    public static class $NO_DATA_FOUND extends RuntimeException {}

    public static class $PROGRAM_ERROR extends RuntimeException {}

    public static class $ROWTYPE_MISMATCH extends RuntimeException {}

    public static class $STORAGE_ERROR extends RuntimeException {}

    public static class $TOO_MANY_ROWS extends RuntimeException {}

    public static class $VALUE_ERROR extends RuntimeException {}

    public static class $ZERO_DIVIDE extends RuntimeException {}

    public static String $SQLERRM = null;
    public static Integer $SQLCODE = null;
    public static Date $SYSDATE = null;
    public static Integer $NATIVE = null;

    public static void $PUT_LINE(Object s) {
        System.out.println(s);
    }

    // TODO: remove
    public static Integer $OPEN_CURSOR() {
        return -1;
    }

    // TODO: remove
    public static Integer $LAST_ERROR_POSITION() {
        return -1;
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
    public static Boolean opNot(Boolean l) {
        if (l == null) {
            return null;
        }
        return !l;
    }

    // ====================================
    // is null
    public static Boolean opIsNull(Object l) {
        return (l == null);
    }

    // ====================================
    // arithmetic negative
    public static Short opNeg(Short l) {
        if (l == null) {
            return null;
        }
        return ((short) -l);
    }
    public static Integer opNeg(Integer l) {
        if (l == null) {
            return null;
        }
        return -l;
    }
    public static Long opNeg(Long l) {
        if (l == null) {
            return null;
        }
        return -l;
    }
    public static BigDecimal opNeg(BigDecimal l) {
        if (l == null) {
            return null;
        }
        return l.negate();
    }
    public static Float opNeg(Float l) {
        if (l == null) {
            return null;
        }
        return -l;
    }
    public static Double opNeg(Double l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    // ====================================
    // bitwise compliment
    public static Long opBitCompli(Short l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }
    public static Long opBitCompli(Integer l) {
        if (l == null) {
            return null;
        }
        return ~l.longValue();
    }
    public static Long opBitCompli(Long l) {
        if (l == null) {
            return null;
        }
        return ~l;
    }

    // ====================================
    // boolean and
    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    // ====================================
    // boolean or
    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    // ====================================
    // boolean xor
    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    // ====================================
    // comparison equal
    public static Boolean opEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.equals(r);
    }

    // ====================================
    // comparison null safe equal
    public static Boolean opNullSafeEq(Object l, Object r) {
        if (l == null) {
            return (r == null);
        } else if (r == null) {
            return (l == null);
        }
        return l.equals(r);
    }

    // ====================================
    // comparison not equal
    public static Boolean opNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return !l.equals(r);
    }

    // ====================================
    // comparison less than or equal to (<=)
    public static Boolean opLe(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }
    public static Boolean opLe(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }
    public static Boolean opLe(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }
    public static Boolean opLe(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }
    public static Boolean opLe(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }
    public static Boolean opLe(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(ZonedTimestamp l, ZonedTimestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }
    public static Boolean opLe(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }
    public static Boolean opLe(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        SetOrder o = compareMultiSets(l, r);
        return (o == SetOrder.INCLUDED || o == SetOrder.EQUAL);
    }
    public static <E extends Comparable<E>> Boolean opLe(List<E> l, List<E> r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) <= 0;
    }

    // ====================================
    // comparison greater than or equal to (>=)
    public static Boolean opGe(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
    public static Boolean opGe(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }
    public static Boolean opGe(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }
    public static Boolean opGe(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }
    public static Boolean opGe(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
    public static Boolean opGe(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }
    public static Boolean opGe(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }
    public static Boolean opGe(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
    public static Boolean opGe(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
    public static Boolean opGe(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
    public static Boolean opGe(ZonedTimestamp l, ZonedTimestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }
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

    // ====================================
    // comparison less than (<)
    public static Boolean opLt(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
    public static Boolean opLt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }
    public static Boolean opLt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }
    public static Boolean opLt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }
    public static Boolean opLt(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
    public static Boolean opLt(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }
    public static Boolean opLt(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }
    public static Boolean opLt(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
    public static Boolean opLt(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
    public static Boolean opLt(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
    public static Boolean opLt(ZonedTimestamp l, ZonedTimestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }
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

    // ====================================
    // comparison greater than (>)
    public static Boolean opGt(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
    public static Boolean opGt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }
    public static Boolean opGt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }
    public static Boolean opGt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }
    public static Boolean opGt(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
    public static Boolean opGt(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }
    public static Boolean opGt(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }
    public static Boolean opGt(Date l, Date r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
    public static Boolean opGt(Time l, Time r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
    public static Boolean opGt(Timestamp l, Timestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
    public static Boolean opGt(ZonedTimestamp l, ZonedTimestamp r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
    }
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

    // ====================================
    // between
    public static Boolean opBetween(String o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
    public static Boolean opBetween(Short o, Short lower, Short upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }
    public static Boolean opBetween(Integer o, Integer lower, Integer upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }
    public static Boolean opBetween(Long o, Long lower, Long upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }
    public static Boolean opBetween(BigDecimal o, BigDecimal lower, BigDecimal upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
    public static Boolean opBetween(Float o, Float lower, Float upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }
    public static Boolean opBetween(Double o, Double lower, Double upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o >= lower && o <= upper;
    }
    public static Boolean opBetween(Date o, Date lower, Date upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
    public static Boolean opBetween(Time o, Time lower, Time upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
    public static Boolean opBetween(Timestamp o, Timestamp lower, Timestamp upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }
    public static Boolean opBetween(ZonedTimestamp o, ZonedTimestamp lower, ZonedTimestamp upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return o.compareTo(lower) >= 0 && o.compareTo(upper) <= 0;
    }

    // ====================================
    // in
    public static Boolean opIn(Object o, Object... list) {
        if (o == null || list == null) {
            return null;
        }
        for (Object p : list) {
            if (Objects.equals(o, p)) {
                return true;
            }
        }
        return false;
    }

    // ====================================
    // *
    public static Short opMult(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l * r);
    }
    public static Integer opMult(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }
    public static Long opMult(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }
    public static BigDecimal opMult(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.multiply(r);
    }
    public static Float opMult(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }
    public static Double opMult(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }
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


    // ====================================
    // /
    public static Short opDiv(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }
    public static Integer opDiv(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }
    public static Long opDiv(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }
    public static BigDecimal opDiv(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.divide(r, BigDecimal.ROUND_HALF_UP);
    }
    public static Float opDiv(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }
    public static Double opDiv(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }


    // ====================================
    // DIV
    public static Short opDivInt(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l / r);
    }
    public static Integer opDivInt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }
    public static Long opDivInt(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    // ====================================
    // MOD
    public static Short opMod(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l % r);
    }
    public static Integer opMod(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }
    public static Long opMod(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    // ====================================
    // +
    public static Short opAdd(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l + r);
    }
    public static Integer opAdd(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }
    public static Long opAdd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }
    public static BigDecimal opAdd(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.add(r);
    }
    public static Float opAdd(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }
    public static Double opAdd(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }
    public static LocalTime opAdd(LocalTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.plusSeconds(r.longValue());
    }
    public static LocalDate opAdd(LocalDate l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.plusDays(r.longValue());
    }
    public static LocalDateTime opAdd(LocalDateTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.plus(r.longValue(), ChronoUnit.MILLIS);
    }
    public static ZonedDateTime opAdd(ZonedDateTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.plusSeconds(r.longValue());
    }
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


    // ====================================
    // -
    public static Short opSubtract(Short l, Short r) {
        if (l == null || r == null) {
            return null;
        }
        return (short) (l - r);
    }
    public static Integer opSubtract(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }
    public static Long opSubtract(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }
    public static BigDecimal opSubtract(BigDecimal l, BigDecimal r) {
        if (l == null || r == null) {
            return null;
        }
        return l.subtract(r);
    }
    public static Float opSubtract(Float l, Float r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }
    public static Double opSubtract(Double l, Double r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }
    public static Long opSubtract(LocalTime l, LocalTime r) {
        if (l == null || r == null) {
            return null;
        }
        return r.until(l, ChronoUnit.SECONDS);
    }
    public static Long opSubtract(LocalDate l, LocalDate r) {
        if (l == null || r == null) {
            return null;
        }
        return r.until(l, ChronoUnit.DAYS);
    }
    public static Long opSubtract(LocalDateTime l, LocalDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return r.until(l, ChronoUnit.MILLIS);
    }
    public static Long opSubtract(ZonedDateTime l, ZonedDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return r.until(l, ChronoUnit.SECONDS);
    }
    public static LocalTime opSubtract(LocalTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.minusSeconds(r.longValue());
    }
    public static LocalDate opSubtract(LocalDate l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.minusDays(r.longValue());
    }
    public static LocalDateTime opSubtract(LocalDateTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.minus(r.longValue(), ChronoUnit.MILLIS);
    }
    public static ZonedDateTime opSubtract(ZonedDateTime l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l.minusSeconds(r.longValue());
    }
    public static Long opSubtract(LocalDate l, ZonedDateTime r)     {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(ZonedDateTime.of(l, LocalTime.MIN, r.getZone()), r);
    }
    public static Long opSubtract(LocalDate l, LocalDateTime r)     {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(LocalDateTime.of(l, LocalTime.MIN), r);
    }
    public static Long opSubtract(LocalDateTime l, LocalDate r)     {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(l, LocalDateTime.of(r, LocalTime.MIN));
    }
    public static Long opSubtract(LocalDateTime l, ZonedDateTime r) {
        if (l == null || r == null) {
            return null;
        }
        return opSubtract(l, r.toLocalDateTime());
    }
    public static Long opSubtract(ZonedDateTime l, LocalDate r)     {
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


    // ====================================
    // ||
    public static String opConcat(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.toString() + r.toString();
    }

    // ====================================
    // <<
    public static Long opBitShiftLeft(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l << r;
    }

    // ====================================
    // >>
    public static Long opBitShiftRight(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l >> r;
    }

    // ====================================
    // &
    public static Long opBitAnd(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l & r;
    }

    // ====================================
    // ^
    public static Long opBitXor(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l ^ r;
    }

    // ====================================
    // |
    public static Long opBitOr(Long l, Long r) {
        if (l == null || r == null) {
            return null;
        }
        return l | r;
    }

    public static Boolean opLike(String s, String pattern, String escape) {
        return false;
    }

    // ------------------------------------------------
    // Private
    // ------------------------------------------------

    // set and multiset ordering
    private enum SetOrder {
        EQUAL,
        INCLUDING,
        INCLUDED,
        NONE,
    }

    private static SetOrder compareSets(Set l, Set r) {
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

    private static SetOrder compareMultiSets(MultiSet l, MultiSet r) {
        assert l != null && r != null;

        if (l.equals(r)) {
            return SetOrder.EQUAL;
        } else if (l.containsAll(r)) {
            Set s = r.uniqueSet();
            for (Object o: s) {
                int ln = l.getCount(o);
                int rn = r.getCount(o);
                if (ln < rn) {
                    return SetOrder.NONE;
                }
            }
            return SetOrder.INCLUDING;
        } else if (r.containsAll(l)) {
            Set s = l.uniqueSet();
            for (Object o: s) {
                int ln = l.getCount(o);
                int rn = r.getCount(o);
                if (ln > rn) {
                    return SetOrder.NONE;
                }
            }
            return SetOrder.INCLUDED;
        }

        return SetOrder.NONE;
    }

    private static <E extends Comparable<E>> int compareLists(List<E> l, List<E> r) {
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

    private static MultiSet set2MultiSet(Set s) {
        assert s != null;
        return new HashMultiSet(s);
    }

    private static MultiSet list2MultiSet(List l) {
        assert l != null;
        return new HashMultiSet(l); // TODO: check if the elements duplicate in the list has count larger than 1
    }

    private static List concatLists(List l, List r) {
        ArrayList ret = new ArrayList();
        ret.addAll(l);
        ret.addAll(r);
        return ret;
    }

    private static Set unionSets(Set l, Set r) {
        HashSet ret = new HashSet();
        ret.addAll(l);
        ret.addAll(r);
        return ret;
    }

    private static Set diffSets(Set l, Set r) {
        HashSet ret = new HashSet();
        for (Object o: l) {
            if (!r.contains(o)) {
                ret.add(o);
            }
        }
        return ret;
    }

    private static Set intersectSets(Set l, Set r) {
        HashSet ret = new HashSet();
        for (Object o: l) {
            if (r.contains(o)) {
                ret.add(o);
            }
        }
        return ret;
    }

    private static MultiSet unionMultiSets(MultiSet l, MultiSet r) {
        throw new RuntimeException("unimplemented yet");
    }

    private static MultiSet diffMultiSets(MultiSet l, MultiSet r) {
        throw new RuntimeException("unimplemented yet");
    }

    private static MultiSet intersectMultiSets(MultiSet l, MultiSet r) {
        throw new RuntimeException("unimplemented yet");
    }
}
