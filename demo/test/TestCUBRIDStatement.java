package test;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Types;
import test.SqlUtil.Arg;

/*
 * CREATE TABLE test ( val INT );
 * CREATE OR REPLACE FUNCTION TC_SX() RETURN STRING as language java name 'TestCUBRIDStatement.testX() return java.lang.String';
 */
public class TestCUBRIDStatement {

    public static String test0() throws SQLException, ClassNotFoundException {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

            SqlUtil.createTable(conn, "t1", "a int");
            SqlUtil.dropTable(conn, "t1");
        } catch (Exception e) {
            return "f";
        }

        return "t";
    }

    public static String test1() throws SQLException, ClassNotFoundException {
        Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");
        Statement stmt = conn.createStatement();
        try {
            stmt.executeQuery("update t1 set a=1 where a=2");
            return "f";
        } catch (SQLException e) {
            // return "f"; // ?
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    public static String test2() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");
        Statement stmt = conn.createStatement();

        try {
            int res = stmt.executeUpdate("update t1 set a=1 where a=2");
            if (res == 0) {
                return "t";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "f";
    }

    public static String test3() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.setMaxFieldSize(6);

        if (stmt.getMaxFieldSize() != 6) {
            return "f";
        }

        SqlUtil.createTable(conn, "t1", "a varchar(10)");
        try {
            SqlUtil.insertRow(conn, "t1", new Arg("a", Types.VARCHAR, "0123456789"));
            ResultSet rs = stmt.executeQuery("select * from t1");
            if (rs.next() != true) {
                return "f";
            }
            String str = rs.getString("a");
            if ("012345" != str) {
                return "f";
            }

        } finally {
            stmt.close();
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    /* max rows */
    public static String test4() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");

        try {
            for (int i = 0; i < 10; i++) {
                SqlUtil.insertRow(conn, "t1", new Arg("a", Types.INTEGER, i));
            }

            Statement stmt = conn.createStatement();
            stmt.setMaxRows(3);
            ResultSet rs = stmt.executeQuery("select * from t1");

            for (int i = 0; i < 3; i++) {
                if (rs.next() != true) {
                    return "f";
                }
            }

            if (rs.next() != false) {
                return "f";
            }

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    public static String test4a() throws SQLException {
        try {
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            Statement stmt = conn.createStatement();
            stmt.setMaxRows(-1);
        } catch (IllegalArgumentException e) {
            return "t";
        }
        return "f";
    }

    public static String test4b() throws SQLException {
        try {
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            Statement stmt = conn.createStatement();
            stmt.setMaxFieldSize(-1);
        } catch (IllegalArgumentException e) {
            return "t";
        }
        return "f";
    }

    public static String test4c() throws SQLException {
        try {
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            Statement stmt = conn.createStatement();
            stmt.setQueryTimeout(-1);
        } catch (IllegalArgumentException e) {
            return "t";
        }
        return "f";
    }

    public static String test5() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();

        /* do nothing */
        stmt.setEscapeProcessing(true);
        stmt.setEscapeProcessing(false);

        return "t";
    }

    /* getQueryplan is not supported by Server-side JDBC
       public static String test6() throws SQLException {
           Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
           SqlUtil.createTable(conn, "code", "a int");
    	CUBRIDStatement stmt = (CUBRIDStatement) conn.createStatement();
    	stmt.setQueryInfo(true);
    	String str = stmt.getQueryplan("select * from code");
    	System.out.println(str);

    	SqlUtil.dropTable(conn, "code");
    }
       */

    /* holdability is not supported by Server-side JDBC
       public void test7() throws SQLException {
    	CUBRIDConnection conn = ConnectionProvider.getConnection();
    	conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
    	SqlUtil.createTable(conn, "code", "a int");
    	CUBRIDStatement stmt = (CUBRIDStatement) conn.createStatement();
    	stmt.setQueryInfo(true);
    	stmt.executeQuery("select * from code");
    	System.out.println(stmt.getQueryplan());
    	SqlUtil.dropTable(conn, "code");
    }
       */

    public static String test8() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.clearWarnings();
        if (null != stmt.getWarnings()) {
            return "f";
        }
        return "t";
    }

    public static String test9() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.setCursorName("aabbcc"); // do nothing
        return "t";
    }

    public static String test10() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        try {
            stmt.setFetchDirection(ResultSet.FETCH_FORWARD);
            return "f";
        } catch (SQLException e) {
        }

        stmt = conn.createStatement(ResultSet.TYPE_SCROLL_INSENSITIVE, ResultSet.CONCUR_READ_ONLY);
        stmt.setFetchDirection(ResultSet.FETCH_REVERSE);

        if (ResultSet.FETCH_REVERSE != stmt.getFetchDirection()) {
            return "f";
        }

        try {
            stmt.setFetchDirection(102030);
            return "f";
        } catch (IllegalArgumentException e) {
        }

        return "t";
    }

    public static String test11() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();

        stmt.setFetchSize(10);

        if (10 != stmt.getFetchSize()) {
            return "f";
        }

        try {
            stmt.setFetchSize(-1);
            return "f";
        } catch (IllegalArgumentException e) {
        }

        return "t";
    }

    // TODO
    /*
    public static String test12() {
           Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	Statement stmt = conn.createStatement();
    	SqlUtil.createTable(conn, "t1", "a int auto_increment", "b int");
    	try {
    		CUBRIDOID oid = stmt.executeInsert("insert into t1 (b) values (1)");
    	} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}

           return "t";
    }
       */

    // TODO stmt.getStatementType() is not a standard
    /*
    public static String test13() {
           Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	Statement stmt = conn.createStatement();

           if (stmt.getStatementType() != CUBRIDCommandType.CUBRID_STMT_UNKNOWN)
           {
               return "f";
           }

    	stmt.execute("select * from db_root");

           if (stmt.getStatementType() != CUBRIDCommandType.CUBRID_STMT_UNKNOWN)
           {
               return "f";
           }

    	Assert.assertEquals(CUBRIDCommandType.CUBRID_STMT_SELECT,
    			stmt.getStatementType());
    }
       */

    public static String test16() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");
        try {
            ResultSet rs = SqlUtil.executeQuery(conn, "select * from t1");
            rs.close();
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    /* TODO: Testing for this is not needed
    // Add test case For http://bts4.nhncorp.com/nhnbts/browse/CUBRIDSUS-10706
    public static String test17() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        try {
            Statement stmt = conn.createStatement();
            try {
                stmt.setQueryTimeout(-1);
                return "f";
            } catch (IllegalArgumentException e) {

            }

            PreparedStatement pstmt1 = conn.prepareStatement("select * from db_root");
            try {
                pstmt1.setQueryTimeout(-1);
                return "f";
            } catch (IllegalArgumentException e) {
            }

            stmt.close();
            pstmt1.close();
        } finally {
        }

        return "t";
    }
    */
}
