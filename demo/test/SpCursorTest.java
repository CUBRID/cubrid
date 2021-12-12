/*
 *
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

package test;

import java.sql.CallableStatement;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.Statement;
import java.sql.Types;

public class SpCursorTest {
    public static ResultSet TResultSet() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:");

            String sql = "select name, nation_code, event from athlete";
            Statement stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(sql);
            return rs;
        } catch (Exception e) {
            e.printStackTrace();
        }

        return null;
    }

    public static ResultSet TResultSetWithLimit(int limit) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:");

            String sql = "select name, nation_code, event from athlete LIMIT " + limit;
            Statement stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(sql);
            return rs;
        } catch (Exception e) {
            e.printStackTrace();
        }

        return null;
    }

    public static String PyramidPrinter(int depth) {
        String result = "";
        if (depth >= 0) {
            try {
                Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
                Connection conn = DriverManager.getConnection("jdbc:default:connection:");

                for (int i = 0; i < depth; i++) {
                    CallableStatement cstmt =
                            conn.prepareCall("?=CALL rset_limit(" + (i + 1) + ")");
                    cstmt.registerOutParameter(1, Types.JAVA_OBJECT);
                    cstmt.execute();
                    ResultSet rs = (ResultSet) cstmt.getObject(1);

                    while (rs.next()) {
                        result += rs.getString(1) + "|";
                    }
                    result += "\n";

                    rs.close();
                }
            } catch (Exception e) {
                result = e.getMessage();
            }
        }

        return result;
    }

    public static ResultSet testCursorWithQuery(String sql) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection con = DriverManager.getConnection("jdbc:default:connection:");

            String query = sql;
            Statement stmt = con.createStatement();
            ResultSet rs = stmt.executeQuery(query);
            return rs;
        } catch (Exception e) {
            // e.printStackTrace();
        }
        return null;
    }

    public static String testOutResultWithQuery(String query) {
        String ret = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection con = DriverManager.getConnection("jdbc:default:connection:");

            CallableStatement cstmt = con.prepareCall("? = CALL rset_q(?)");
            cstmt.registerOutParameter(1, Types.JAVA_OBJECT);
            cstmt.registerOutParameter(2, Types.VARCHAR);
            cstmt.setObject(2, query);
            cstmt.execute();
            ResultSet rs = (ResultSet) cstmt.getObject(1);
            rs = (ResultSet) cstmt.getObject(1);
            ResultSetMetaData rsmd = rs.getMetaData();
            int numberofColumn = rsmd.getColumnCount();

            for (int i = 1; i <= numberofColumn; i++) {
                String ColumnName = rsmd.getColumnName(i);
                ret = ret + ColumnName + "|";
            }

            while (rs.next()) {
                for (int j = 1; j <= numberofColumn; j++) {
                    ret = ret + rs.getObject(j) + "|";
                }
            }
            rs.close();
        } catch (Exception e) {
            ret = e.getMessage();
        }
        return ret;
    }
}
