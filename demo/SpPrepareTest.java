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

// import org.json.simple.JSONArray;
// import org.json.simple.JSONObject;

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
        for ( int i = 0; i< 10000; i++)
            temp = temp + "1234567890";
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
                String fiboStmt = "SELECT testFiboSP (?) + testFibo (?);";
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
                result = rs.getInt(1);
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
            if (!where.isEmpty()) {
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
     * CREATE OR REPLACE FUNCTION testSubquery(select_list string, from_claus string, where_claus string, value string) RETURN string as language java name 'SpPrepareTest.test(java.lang.String, java.lang.String, java.lang.String, java.lang.String) return java.lang.String';
     * SELECT h.host_year, testSubquery('o.host_nation', 'olympic o', 'o.host_year', h.host_year) AS host_nation, h.event_code, h.score, h.unit FROM history2 h LIMIT 20
     */
    public static String testSubquery2(
            String select_list, String from, String where, String value) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String query =
                    String.format("select %s from %s where %s = ?", select_list, from, where);
            PreparedStatement pstmt = conn.prepareStatement(query);
            pstmt.setString(1, value);
            ResultSet rs = pstmt.executeQuery();

            String result = "";
            // StringBuilder builder = new StringBuilder();
            // int i = 0;
            while (rs.next()) {
                // i++;
                // builder.append(rs.getString(1));
                // esult += rs.getString(1);
                rs.getString(1);
            }
            // result = builder.toString();

            rs.close();
            pstmt.close();
            conn.close();
            return result;
        } catch (Exception e) {
            e.printStackTrace();
            return e.getMessage();
        }
    }

    public static String test3(String tbl, String col_name, String value) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String query = "select host_nation from " + tbl + " where " + col_name + " = ?";
            PreparedStatement pstmt = conn.prepareStatement(query);
            pstmt.setString(1, value);
            ResultSet rs = pstmt.executeQuery();
            rs.next();
            return rs.getString(1);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return "";
    }

    public static String test2(String tbl, String col_name, String value) {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String query = "EXECUTE test_stmt USING " + value;
            PreparedStatement pstmt = conn.prepareStatement(query);
            ResultSet rs = pstmt.executeQuery();
            rs.next();
            return rs.getString(1);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return "";
    }

    // CREATE OR REPLACE FUNCTION JSON_QUERY_LINK(q STRING) RETURN STRING AS LANGUAGE JAVA NAME
    // 'SpPrepareTest.json_link(java.lang.String) return java.lang.String';
    // Simple Test: SELECT JSON_QUERY_LINK ('select * from game LIMIT 100');

    /*
     select tbl.event_code, tbl.athlete_code from json_table (
     JSON_QUERY_LINK('select event_code, athlete_code from game'),
    '$.*' COLUMNS ( NESTED PATH '$' COLUMNS (event_code int path '$.c1',
                                                athlete_code int path '$.c2'
                                               ))
    ) as tbl, event e
    where tbl.event_code = e.code;

    SELECT * FROM (select tbl.event_code, tbl.athlete_code from json_table (
     JSON_QUERY_LINK('select event_code, athlete_code from game'),
    '$.*' COLUMNS ( NESTED PATH '$' COLUMNS (event_code int path '$.c1',
                                                athlete_code int path '$.c2'
                                               ))
    ) as tbl) as jtbl, event e
    where jtbl.event_code = e.code;


    select tbl.event_code from json_table (
     JSON_QUERY_LINK('select * from game'),
    '$.*' COLUMNS ( NESTED PATH '$' COLUMNS (event_code int path '$.c1'
                                               ))
    ) as tbl
     */
    public static String json_link(String query) {
        StringBuilder builder = new StringBuilder();
        // JSONArray json = new JSONArray();
        try {
            // Connection
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            // Execute
            PreparedStatement pstmt = conn.prepareStatement(query);
            ResultSet rs = pstmt.executeQuery();
            ResultSetMetaData rsmd = rs.getMetaData();
            // Create JSON Object of each column of the query

            int num = 1;
            builder.append("{");
            while (rs.next()) {
                int numColumns = rsmd.getColumnCount();
                String numStr = String.valueOf(num);
                builder.append("\"r" + numStr + "\":");
                builder.append("{");
                for (int i = 1; i < numColumns + 1; i++) {
                    String column_name = rsmd.getColumnName(i);

                    if (rsmd.getColumnType(i) == java.sql.Types.ARRAY) {
                        builder.append("\"c" + i + "\":" + rs.getArray(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.BOOLEAN) {
                        builder.append("\"c" + i + "\":" + rs.getBoolean(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.BLOB) {
                        builder.append("\"c" + i + "\":" + rs.getBlob(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.DOUBLE) {
                        builder.append("\"c" + i + "\":" + rs.getDouble(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.FLOAT) {
                        builder.append("\"c" + i + "\":" + rs.getFloat(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.NVARCHAR) {
                        builder.append("\"c" + i + "\":\"" + rs.getNString(column_name) + "\",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.VARCHAR) {
                        builder.append("\"c" + i + "\":\"" + rs.getString(column_name) + "\",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.INTEGER
                            || rsmd.getColumnType(i) == java.sql.Types.TINYINT
                            || rsmd.getColumnType(i) == java.sql.Types.SMALLINT
                            || rsmd.getColumnType(i) == java.sql.Types.BIGINT) {
                        builder.append("\"c" + i + "\":" + rs.getInt(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.DATE) {
                        builder.append("\"c" + i + "\":\"" + rs.getDate(column_name) + "\",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.TIMESTAMP) {
                        builder.append("\"c" + i + "\":\"" + rs.getTimestamp(column_name) + "\",");
                    } else {
                        builder.append("\"c" + i + "\":\"" + rs.getObject(column_name) + "\",");
                    }
                }
                builder.deleteCharAt(builder.length() - 1);
                builder.append("},");
                num++;
            }
            builder.deleteCharAt(builder.length() - 1);
            builder.append("}");
        } catch (Exception e) {
            e.printStackTrace();
            return "";
        }
        return builder.toString();
    }

    public static String json_link_with_library(String query) {
        StringBuilder builder = new StringBuilder();
        // JSONArray json = new JSONArray();
        try {
            // Connection
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            // Execute
            PreparedStatement pstmt = conn.prepareStatement(query);
            ResultSet rs = pstmt.executeQuery();
            ResultSetMetaData rsmd = rs.getMetaData();
            // Create JSON Object of each column of the query
            int num = 1;

            builder.append("{");
            while (rs.next()) {
                int numColumns = rsmd.getColumnCount();
                // JSONObject obj = new JSONObject();
                builder.append("\"" + String.valueOf(num) + "\":");
                builder.append("{");
                for (int i = 1; i < numColumns + 1; i++) {
                    String column_name = rsmd.getColumnName(i);

                    if (rsmd.getColumnType(i) == java.sql.Types.ARRAY) {
                        builder.append(
                                "\"" + column_name + "\" :" + rs.getArray(column_name) + ",");
                        // obj.put(column_name, rs.getArray(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.BOOLEAN) {
                        // obj.put(column_name, rs.getBoolean(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.BLOB) {
                        // obj.put(column_name, rs.getBlob(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.DOUBLE) {
                        // obj.put(column_name, rs.getDouble(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.FLOAT) {
                        builder.append(
                                "\"" + column_name + "\" :" + rs.getFloat(column_name) + ",");
                        // obj.put(column_name, rs.getFloat(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.NVARCHAR) {
                        builder.append(
                                "\"" + column_name + "\" :" + rs.getNString(column_name) + ",");
                        // obj.put(column_name, rs.getNString(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.VARCHAR) {
                        builder.append(
                                "\"" + column_name + "\" : \"" + rs.getString(column_name) + "\",");
                        // obj.put(column_name, rs.getString(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.INTEGER
                            || rsmd.getColumnType(i) == java.sql.Types.TINYINT
                            || rsmd.getColumnType(i) == java.sql.Types.SMALLINT
                            || rsmd.getColumnType(i) == java.sql.Types.BIGINT) {
                        // obj.put(column_name, rs.getInt(column_name));
                        builder.append("\"" + column_name + "\" :" + rs.getInt(column_name) + ",");
                    } else if (rsmd.getColumnType(i) == java.sql.Types.DATE) {
                        // obj.put(column_name, rs.getDate(column_name));
                    } else if (rsmd.getColumnType(i) == java.sql.Types.TIMESTAMP) {
                        // obj.put(column_name, rs.getTimestamp(column_name));
                    } else {
                        // obj.put(column_name, rs.getObject(column_name));
                    }
                }
                builder.deleteCharAt(builder.length() - 1);
                builder.append("},");
                // json.put(obj);
                num++;
            }
            builder.deleteCharAt(builder.length() - 1);
            builder.append("}");
        } catch (Exception e) {
            e.printStackTrace();
        }
        return builder.toString();
    }
}
