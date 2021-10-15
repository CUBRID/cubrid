package test;

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

import java.sql.*;

/*
 * [@demodb]
 * java_stored_procedure=yes
 * java_stored_procedure_port=9999
 */

public class SpPrepareTest {

    /*
     * CREATE OR REPLACE FUNCTION TestReturnString() RETURN STRING
     * AS LANGUAGE JAVA
     * NAME 'SpCubrid.testReturnString() return java.lang.String';
     */
    public static String testReturnString() {
        String temp = "";
        for (int i = 0; i < 10000; i++) temp = temp + "1234567890";
        return temp;
    }

    /*
     * CREATE OR REPLACE FUNCTION hello() RETURN STRING
     * AS LANGUAGE JAVA
     * NAME 'SpCubrid.HelloCubrid() return java.lang.String';
     */
    public static String testHello() {
        return "Hello, Cubrid !!";
    }

    /*
     * CREATE FUNCTION testPlusSP(a INT, b INT) RETURN int as language java name 'SpPrepareTest.testWithoutJDBC(int, int) return int';
     * SELECT testPlusSP (1, 2);
     */
    public static int testWithoutJDBC(int a, int b) {
        return a + b;
    }

    /*
     * NESTED CALL
     * CREATE OR REPLACE FUNCTION testFiboSP(n INT) RETURN int as language java name 'SpPrepareTest.testFibo(int) return int';
     * SELECT testFiboSP (5);
     */
    public static int testFibo(int n) {
        try {
            if (n == 0) {
                return 0;
            } else if (n == 1) {
                return 1;
            } else {
                int result = 0;
                Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
                Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
                String fiboStmt = "SELECT testFiboSP (?) + testFiboSP (?);";
                PreparedStatement stmt = conn.prepareStatement(fiboStmt);
                stmt.setInt(1, n - 1);
                stmt.setInt(2, n - 2);

                ResultSet rs = stmt.executeQuery();
                rs.next();
                result += rs.getInt(1);
                return result;
            }
        } catch (Exception e) {
            e.printStackTrace();
            return -1;
        }
    }

    /*
     * DELETE, INSERT, SELECT
     * CREATE TABLE test ( val INT );
     * CREATE OR REPLACE FUNCTION testInsert(val INT, delete INT) RETURN int as language java name 'SpPrepareTest.testInsert(int, int) return int';
     */
    public static int testInsert(int value, int delete) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            if (delete == 1) {
                String drop_q = "DELETE FROM test;";
                PreparedStatement pstmt = conn.prepareStatement(drop_q);
                pstmt.executeUpdate();
                pstmt.close();
            }

            for (int i = 0; i < value; i++) {
                String ins_q = "INSERT INTO test VALUES (?);";
                PreparedStatement pstmt = conn.prepareStatement(ins_q);
                pstmt.setInt(1, value);
                pstmt.executeUpdate();
                pstmt.close();
            }

            int sum = 0;
            String sel_q = "SELECT * FROM test";
            PreparedStatement pstmt = conn.prepareStatement(sel_q);
            ResultSet rs = pstmt.executeQuery();
            while (rs.next()) {
                sum += rs.getInt(1);
            }
            rs.close();
            pstmt.close();

            int cnt = 0;
            String query = "SELECT COUNT (*) FROM test;";
            PreparedStatement pstmt2 = conn.prepareStatement(query);
            ResultSet rs2 = pstmt2.executeQuery();
            if (rs2.next()) {
                cnt = rs2.getInt(1);
            }
            rs2.close();
            pstmt2.close();

            int result = 0;
            if (cnt != 0) {
                result = sum / cnt;
            }

            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1;
        }
    }

    /*
     * CREATE OR REPLACE FUNCTION testDummy(n int) RETURN int as language java name 'SpPrepareTest.testDummy(int) return int';
     * SELECT testDummy (athlete_code) from game;
     * SELECT h.host_year, testSubquery('o.host_nation', 'olympic o', 'o.host_year', h.host_year) AS host_nation, h.event_code, h.score, h.unit FROM history2 h LIMIT 20
     */
    public static int testDummy(int value) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            String query = "select event_code from game";
            PreparedStatement pstmt = conn.prepareStatement(query);
            ResultSet rs = pstmt.executeQuery();

            int result = 0;
            while (rs.next()) {
                result += rs.getInt(1);
                break;
            }

            result += value;

            rs.close();
            pstmt.close();
            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return -1;
        }
    }

    /*
     * CREATE OR REPLACE FUNCTION testSubquery(select_list string, from_claus string, where_claus string, value string) RETURN string as language java name 'SpPrepareTest.test(java.lang.String, java.lang.String, java.lang.String, java.lang.String) return java.lang.String';
     * SELECT h.host_year, testSubquery('o.host_nation', 'olympic o', 'o.host_year', h.host_year) AS host_nation, h.event_code, h.score, h.unit FROM history2 h LIMIT 20
     */
    public static String testSubquery(String select_list, String from, String where, String value) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            String query = null;
            if (where == null || where.isEmpty()) {
                query = String.format("select %s from %s", select_list, from);
            } else {
                query = String.format("select %s from %s where %s = ?", select_list, from, where);
            }
            PreparedStatement pstmt = conn.prepareStatement(query);
            if (where != null && !where.isEmpty()) {
                pstmt.setString(1, value);
            }
            ResultSet rs = pstmt.executeQuery();

            String result = "";
            while (rs.next()) {
                result = rs.getString(1);
                break;
            }

            rs.close();
            pstmt.close();
            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }

    /*
     * CREATE OR REPLACE FUNCTION testStatementRs(q string) RETURN STRING AS LANGUAGE JAVA NAME 'SpPrepareTest.testStatement(java.lang.String) return java.lang.String';
     * SELECT testStatementRs ('SELECT * from game');
     */
    public static String testStatement(String query) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            Statement stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(query);

            String result = "";
            while (rs.next()) {
                result = rs.getString(1);
                break;
            }

            rs.close();
            stmt.close();
            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }
}
