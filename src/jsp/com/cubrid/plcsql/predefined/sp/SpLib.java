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

import java.sql.*;
import java.util.Date;
import java.util.Objects;

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

    public static Boolean opNot(Boolean l) {
        if (l == null) {
            return null;
        }
        return !l;
    }

    public static Boolean opIsNull(Object l) {
        return (l == null);
    }

    public static Integer opNeg(Integer l) {
        if (l == null) {
            return null;
        }
        return -l;
    }

    public static Boolean opAnd(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l && r;
    }

    public static Boolean opOr(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return l || r;
    }

    public static Boolean opXor(Boolean l, Boolean r) {
        if (l == null || r == null) {
            return null;
        }
        return (l && !r) || (!l && r);
    }

    public static Boolean opEq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return l.equals(r);
    }

    public static Boolean opNeq(Object l, Object r) {
        if (l == null || r == null) {
            return null;
        }
        return !l.equals(r);
    }

    public static Boolean opLe(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l <= r;
    }

    public static Boolean opGe(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l >= r;
    }

    public static Boolean opLt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l < r;
    }

    public static Boolean opGt(Integer l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return l > r;
    }

    public static Boolean opLt(String l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opGt(String l, Integer r) {
        if (l == null || r == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opLt(Integer l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opGt(Integer l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return false; // TODO
    }

    public static Boolean opLe(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) <= 0;
    }

    public static Boolean opGe(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) >= 0;
    }

    public static Boolean opLt(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) < 0;
    }

    public static Boolean opGt(String l, String r) {
        if (l == null || r == null) {
            return null;
        }
        return l.compareTo(r) > 0;
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
}
