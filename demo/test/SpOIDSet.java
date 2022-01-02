package test;

/**
 * Copyright (c) 2016, Search Solution Corporation. All rights reserved.
 *
 * <p>Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * <p>* Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * <p>* Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided with
 * the distribution.
 *
 * <p>* Neither the name of the copyright holder nor the names of its contributors may be used to
 * endorse or promote products derived from this software without specific prior written permission.
 *
 * <p>THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
import cubrid.jdbc.driver.*;
import cubrid.sql.*;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.*;

public class SpOIDSet {

    /* from SpTest6.testoid2 */
    public static String testoid1(CUBRIDOID oid) {
        Connection conn = null;
        Statement stmt = null;
        String ret = "";

        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String[] attrs = {"id"};

            Integer[] values = {new Integer(10)};

            conn.setAutoCommit(false);
            ret = ret + oid.getTableName() + " | ";
            ret = ret + oid.isInstance() + " | ";
            ResultSet rs = (ResultSet) oid.getValues(attrs);
            while (rs.next()) {
                ret = ret + " || " + rs.getString(1);
            }
            oid.setValues(attrs, values);
            oid.getValues(attrs);
            rs = (ResultSet) oid.getValues(attrs);
            while (rs.next()) {
                ret = ret + " || " + rs.getString(1);
            }
            oid.remove();
            conn.close();
            return ret;
        } catch (SQLException e) {
            // e.printStackTrace();
        } catch (Exception e) {
            // e.printStackTrace();
        }
        return "";
    }

    /* from SpTest6.testoid4 */
    public static CUBRIDOID testoid2(String query) {
        Connection conn = null;
        Statement stmt = null;
        String ret = "";

        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            conn.setAutoCommit(false);
            stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(query);

            ResultSetMetaData rsmd = rs.getMetaData();
            int numberofColumn = rsmd.getColumnCount();

            while (rs.next()) {
                return (CUBRIDOID) rs.getObject(1);
            }
            stmt.close();
            conn.close();
        } catch (SQLException e) {
            // e.printStackTrace();
        } catch (Exception e) {
            // e.printStackTrace();
        }
        return null;
    }

    /* from SpTest6.ptestoid1 */
    public static void ptestoid1(CUBRIDOID[] oid, String query) {
        Connection conn = null;
        Statement stmt = null;

        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            conn.setAutoCommit(false);
            stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(query);

            while (rs.next()) {
                oid[0] = (CUBRIDOID) rs.getObject(1);
            }
            stmt.close();
            conn.close();
        } catch (SQLException e) {
            // e.printStackTrace();
        } catch (Exception e) {
            // e.printStackTrace();
        }
    }

    /* from SpTest6.ptestoid3 */
    public static void ptestoid3(cubrid.sql.CUBRIDOID[][] set, cubrid.sql.CUBRIDOID aoid) {
        Connection conn = null;
        Statement stmt = null;
        String ret = "";

        Vector v = new Vector();
        cubrid.sql.CUBRIDOID[] set1 = set[0];

        try {
            if (set1 != null) {
                int len = set1.length;

                int i = 0;
                for (i = 0; i < len; i++) {
                    v.add(set1[i]);
                }
            }
            v.add(aoid);
            set[0] = (cubrid.sql.CUBRIDOID[]) v.toArray(new cubrid.sql.CUBRIDOID[] {});

        } catch (Exception e) {
            // e.printStackTrace();
        }
    }

    /* from SpTest6.testoid1 */
    public static CUBRIDOID testOidArgs1(CUBRIDOID oid) {
        return oid;
    }

    public static String testOidArgs2(CUBRIDOID oid) {
        Connection conn = null;
        Statement stmt = null;
        String ret = "";

        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            String[] attrs = {"id"};

            Integer[] values = {new Integer(10)};

            conn.setAutoCommit(false);
            ret = ret + oid.getTableName() + " | ";
            ret = ret + oid.isInstance() + " | ";
            ResultSet rs = oid.getValues(attrs);
            int i = 0;
            while (rs.next()) {
                ret = ret + " || " + rs.getString(1);
            }
            oid.setValues(attrs, values);
            oid.getValues(attrs);
            rs = oid.getValues(attrs);
            while (rs.next()) {
                ret = ret + " || " + rs.getString(1);
            }
            oid.remove();
            conn.close();
            return ret;
        } catch (SQLException e) {
            // e.printStackTrace();
        } catch (Exception e) {
            // e.printStackTrace();
        }
        return "";
    }

    /* from SpTest6.testoid3 */
    public static java.lang.Object[] testOidArgs3(java.lang.Object[] set) {
        return set;
    }

    /* from SpTest6.testset5 */
    public static void testset5(Integer[][] set) {
        for (int i = 0; i < set[0].length; i++) {
            set[0][i] = new Integer(set[0][i].intValue() + 10);
        }
    }

    public static Float[] testset6(Float[][] set) {
        return set[0];
    }

    public static void testset7(Float[][] set) {}
}
