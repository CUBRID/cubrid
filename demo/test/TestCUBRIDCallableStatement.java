package test;

import java.sql.CallableStatement;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import java.sql.Types;

public class TestCUBRIDCallableStatement {

    public static String test1() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            CallableStatement stmt = conn.prepareCall("? = call hello ()");
            stmt.registerOutParameter(1, Types.VARCHAR);
            stmt.execute();

            return TestUtil.assertEquals("Hello, Cubrid !!", stmt.getString(1));
        } catch (Exception e) {
            return "f";
        }
    }

    public static String test2() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            CallableStatement stmt = conn.prepareCall("? = call sp_plus_int (?, ?)");
            stmt.registerOutParameter(1, Types.INTEGER);
            stmt.setInt (2, 3);
            stmt.setInt (3, 5);

            stmt.execute();

            return TestUtil.assertEquals(8, stmt.getInt(1));
        } catch (Exception e) {
            return "f";
        }
    }

    public static String test3() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            CallableStatement stmt = conn.prepareCall("? = call sp_plus_int_out (?, ?)");
            stmt.registerOutParameter(1, Types.INTEGER);
            stmt.registerOutParameter(2, Types.INTEGER);
            stmt.registerOutParameter(3, Types.INTEGER);

            stmt.setInt (2, 1); // +1 internally
            stmt.setInt (3, 2); // +1 internally

            stmt.execute();

            String result = "";
            result += TestUtil.assertEquals(5, stmt.getInt(1));
            result += TestUtil.assertEquals(2, stmt.getInt(2));
            result += TestUtil.assertEquals(4, stmt.getInt(3));
            return result;
        } catch (Exception e) {
            return "f";
        }
    }
    
    public static String testReturningResultSet(String url) {
        Connection conn = null;
        String result = "";

        try {
            conn = SqlUtil.connect(url,"public","");

            CallableStatement cstmt = conn.prepareCall("?=CALL rset()");
            cstmt.registerOutParameter(1, Types.JAVA_OBJECT);
            cstmt.execute();
            ResultSet rs = (ResultSet) cstmt.getObject(1);

            while(rs.next()) {
                result += rs.getString(1) + "\n";
            }

            rs.close();
        } catch (Exception e) {
            return e.getMessage();
        }

        return result;
    }

    /* first out test */
    private static String test_internal_same_param(
            String name, boolean is_first_out, int type, Object obj) {
        String result = "";
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            String sql = "";
            if (is_first_out) {
                sql += "? = ";
            }
            sql += "call " + name + " (?);";
            CallableStatement stmt = conn.prepareCall(sql);

            int idx = 1;
            if (is_first_out) {
                stmt.registerOutParameter(idx, type);
            }

            switch (type) {
                case Types.BIGINT:
                    break;
            }

            stmt.setObject(idx++, obj, type);
            stmt.execute();

            if (is_first_out) {
                return stmt.getString(1);
            } else {
                return "";
            }
        } catch (Exception e) {
            return "f";
        }
    }

    public static ResultSet TResultSet(){
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:");

            String sql = "select name from athlete";
            Statement stmt = conn.createStatement();
            ResultSet rs = stmt.executeQuery(sql);
            return rs;
        } catch (Exception e) {
            e.printStackTrace();
        }

        return null;
    }
}
