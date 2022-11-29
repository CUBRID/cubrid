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

import java.sql.*;
import java.util.Set;
import java.util.List;
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

    public static Integer $OPEN_CURSOR() {
        return -1; /* TODO */
    }

    public static Integer $LAST_ERROR_POSITION() {
        return -1; /* TODO */
    }

    public static class Query {
        final String query;
        ResultSet rs;

        Query(String query) {
            this.query = query;
        }

        void open(Connection conn, Object... val) throws Exception {
            if (isOpen()) {
                throw new RuntimeException("already open");
            }
            PreparedStatement pstmt = conn.prepareStatement(query);
            for (int i = 0; i < val.length; i++) {
                pstmt.setObject(i + 1, val[i]);
            }
            rs = pstmt.executeQuery();
        }

        void close() throws Exception {
            if (rs != null) {
                Statement stmt = rs.getStatement();
                if (stmt != null) {
                    stmt.close();
                }
                rs = null;
            }
        }

        boolean isOpen() throws Exception {
            return (rs != null && !rs.isClosed());
        }

        boolean found() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow() > 0;
        }

        boolean notFound() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow() == 0;
        }

        int rowCount() throws Exception {
            if (!isOpen()) {
                throw new RuntimeException("invalid cursor");
            }
            return rs.getRow();
        }
    }

    // ------------------------------------
    // operators
    // ------------------------------------

    // boolean not
    public static Boolean opNot(Boolean l) {
        if (l == null) {
            return null;
        }
        return !l;
    }

    // is null
    public static Boolean opIsNull(Object l) {
        return (l == null);
    }

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

    // boolean and
    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    // boolean or
    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    // boolean xor
    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    // comparison equal
    public static Boolean opEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.equals(r);
    }

    // comparison null safe equal
    public static Boolean opNullSafeEq(Object l, Object r) {
        if (l == null) {
            return (r == null);
        } else if (r == null) {
            return (l == null);
        }
        return l.equals(r);
    }

    // comparison not equal
    public static Boolean opNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return !l.equals(r);
    }

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
        return compareSets(l, r) <= 0;
    }
    public static Boolean opLe(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) <= 0;
    }
    public static Boolean opLe(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) <= 0;
    }

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
        return compareSets(l, r) >= 0;
    }
    public static Boolean opGe(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) >= 0;
    }
    public static Boolean opGe(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) >= 0;
    }

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
        return compareSets(l, r) < 0;
    }
    public static Boolean opLt(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) < 0;
    }
    public static Boolean opLt(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) < 0;
    }

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
        return compareSets(l, r) > 0;
    }
    public static Boolean opGt(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) > 0;
    }
    public static Boolean opGt(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareLists(l, r) > 0;
    }

    // comparison set equal
    public static Boolean opSetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) == 0;
    }
    public static Boolean opSetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) == 0;
    }
    public static Boolean opSetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) == 0;
    }
    public static Boolean opSetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) == 0;
    }
    public static Boolean opSetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) == 0;
    }
    public static Boolean opSetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) == 0;
    }
    public static Boolean opSetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) == 0;
    }
    public static Boolean opSetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) == 0;
    }
    public static Boolean opSetEq(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), list2Multiset(r)) == 0;
    }


    // comparison set not equal
    public static Boolean opSetNeq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) != 0;
    }
    public static Boolean opSetNeq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) != 0;
    }
    public static Boolean opSetNeq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) != 0;
    }
    public static Boolean opSetNeq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) != 0;
    }
    public static Boolean opSetNeq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) != 0;
    }
    public static Boolean opSetNeq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) != 0;
    }
    public static Boolean opSetNeq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) != 0;
    }
    public static Boolean opSetNeq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) != 0;
    }
    public static Boolean opSetNeq(List l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), list2Multiset(r)) != 0;
    }

    // comparison superset
    public static Boolean opSuperset(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) > 0;
    }
    public static Boolean opSuperset(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) > 0;
    }
    public static Boolean opSuperset(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) > 0;
    }
    public static Boolean opSuperset(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) > 0;
    }
    public static Boolean opSuperset(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) > 0;
    }
    public static Boolean opSuperset(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) > 0;
    }
    public static Boolean opSuperset(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) > 0;
    }
    public static Boolean opSuperset(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) > 0;
    }

    // comparison subset
    public static Boolean opSubset(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) < 0;
    }
    public static Boolean opSubset(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) < 0;
    }
    public static Boolean opSubset(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) < 0;
    }
    public static Boolean opSubset(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) < 0;
    }
    public static Boolean opSubset(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) < 0;
    }
    public static Boolean opSubset(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) < 0;
    }
    public static Boolean opSubset(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) < 0;
    }
    public static Boolean opSubset(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) < 0;
    }

    // comparison superset or equal
    public static Boolean opSupersetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) >= 0;
    }
    public static Boolean opSupersetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) >= 0;
    }
    public static Boolean opSupersetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) >= 0;
    }
    public static Boolean opSupersetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) >= 0;
    }
    public static Boolean opSupersetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) >= 0;
    }
    public static Boolean opSupersetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) >= 0;
    }
    public static Boolean opSupersetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) >= 0;
    }
    public static Boolean opSupersetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) >= 0;
    }

    // comparison subset or equal
    public static Boolean opSubsetEq(Set l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareSets(l, r) <= 0;
    }
    public static Boolean opSubsetEq(MultiSet l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, set2Multiset(r)) <= 0;
    }
    public static Boolean opSubsetEq(List l, Set r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), set2Multiset(r)) <= 0;
    }
    public static Boolean opSubsetEq(Set l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), r) <= 0;
    }
    public static Boolean opSubsetEq(MultiSet l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, r) <= 0;
    }
    public static Boolean opSubsetEq(List l, MultiSet r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(list2Multiset(l), r) <= 0;
    }
    public static Boolean opSubsetEq(Set l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(set2Multiset(l), list2Multiset(r)) <= 0;
    }
    public static Boolean opSubsetEq(MultiSet l, List r) {
        if (l == null || r == null) {
            return null;
        }
        return compareMultiSets(l, list2Multiset(r)) <= 0;
    }

    public static Boolean opBetween(Integer o, Integer lower, Integer upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opBetween(String o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opBetween(Integer o, String lower, String upper) {
        if (o == null || lower == null || upper == null) {
            return null;
        }
        return false; // TODO
    }

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

    public static Integer opMult(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l * r;
    }

    public static Integer opDiv(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l / r;
    }

    public static Integer opMod(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l % r;
    }

    public static Integer opAdd(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r;
    }

    public static Integer opSubtract(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l - r;
    }

    public static String opConcat(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.toString() + r.toString();
    }

    public static Integer opPower(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return (int) Math.pow(l, r);
    }

    public static String opAdd(String l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l + r; // TODO
    }

    public static Boolean opLike(String s, String pattern, String escape) {
        return false;
    }

    // ------------------------------------------------
    // Private
    // ------------------------------------------------

    private static int compareSets(Set l, Set r) {
        // TODO
        return 0;
    }

    private static int compareMultiSets(MultiSet l, MultiSet r) {
        // TODO
        return 0;
    }

    private static int compareLists(List l, List r) {
        // TODO
        return 0;
    }

    private static MultiSet set2Multiset(Set s) {
        // TODO
        return null;
    }

    private static MultiSet list2Multiset(List l) {
        // TODO
        return null;
    }
}
