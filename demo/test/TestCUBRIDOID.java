package test;

import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.jdbc.CUBRIDServerSideOID;
import com.cubrid.jsp.jdbc.CUBRIDServerSideResultSet;
import cubrid.sql.CUBRIDOID;
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
 * CREATE OR REPLACE FUNCTION TC_OIDX() RETURN STRING as language java name 'TestCUBRIDStatement.testX() return java.lang.String';
 */
public class TestCUBRIDOID {

    public static String test1() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        byte[] data = new byte[] {1, 2, 3, 4, 5, 6, 7, 8};

        String result = "";

        CUBRIDOID oid = new CUBRIDServerSideOID((CUBRIDServerSideConnection) conn, data);
        result += TestUtil.assertArrayEquals(data, oid.getOID());

        CUBRIDOID oid2 = new CUBRIDServerSideOID(oid);
        result += TestUtil.assertArrayEquals(data, oid2.getOID());

        result += TestUtil.assertEquals("@16909060|1286|1800", oid.getOidString());

        CUBRIDOID oid3 = CUBRIDServerSideOID.getNewInstance(conn, oid.getOidString());
        result += TestUtil.assertEquals("@16909060|1286|1800", oid3.getOidString());

        return result;
    }

    public static String test2() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        // conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);

        String result = "";
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = (ResultSet) stmt.executeQuery("select t1 from t1");
            TestUtil.assertEquals(true, rs.next());
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.setValues(new String[] {"a", "b"}, new Object[] {10, "aaaa"});

            rs = (ResultSet) stmt.executeQuery("select * from t1");
            result += TestUtil.assertEquals(true, rs.next());
            result += TestUtil.assertEquals(10, rs.getInt("a"));
            result += TestUtil.assertEquals("aaaa", rs.getString("b"));
            // result += TestUtil.assertEquals(1, rs.getInt("a"));
            // result += TestUtil.assertEquals("a", rs.getString("b"));
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    public static String test3() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        String result = "";
        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = (ResultSet) stmt.executeQuery("select t1 from t1");
            result += TestUtil.assertEquals(true, rs.next());
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.remove();

            rs = stmt.executeQuery("select t1 from t1");
            result += TestUtil.assertEquals(false, rs.next());
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    public static String test4() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        String result = "";
        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = (ResultSet) stmt.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            result += TestUtil.assertEquals(true, oid.isInstance());

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    public static String test5() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = (ResultSet) stmt.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.setReadLock();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    public static String test6() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = stmt.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.setWriteLock();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    public static String test7() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        String result = "";
        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = stmt.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            result += TestUtil.assertEquals("t1", oid.getTableName());
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    public static String test8() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, "t1", "a set(int)", "b sequence(int)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.executeSql(conn, "insert into t1 values (null, null)");
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = stmt.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.addToSet("a", 1);
            oid.addToSet("a", 2);
            oid.addToSet("a", 3);
            oid.removeFromSet("a", 2);

            oid.addToSequence("b", 0, 1);
            oid.addToSequence("b", 1, 2);
            oid.addToSequence("b", 2, 3);
            oid.putIntoSequence("b", 2, 20);
            oid.removeFromSequence("b", 0);
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    /* BLOB and CLOB */
    /*
      public static String test9() throws SQLException {
          Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

          String result = "";
    SqlUtil.createTable(conn, "t1", "a blob", "b clob");

    try {
    	PreparedStatement stmt = conn
    			.prepareStatement("insert into t1 values (?, ?)");
    	stmt.setBlob(1, new ByteArrayInputStream("abcd".getBytes()));
    	stmt.setClob(2, new StringReader("1234"));
    	stmt.execute();
    	stmt.close();

    	Statement stmt2 = conn.createStatement();
    	ResultSet rs = stmt2
    			.executeQuery("select t1 from t1");
    	rs.next();
    	CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
    	ResultSet rs2 = oid.getValues(new String[] { "a", "b" });
    	rs2.next();
    	Blob blob = rs2.getBlob(1);
    	System.out
    			.println(new String(blob.getBytes(1, (int) blob.length())));
    	Clob clob = rs2.getClob(2);
    	System.out.println(clob.getSubString(1, (int) clob.length()));

    } finally {
    	SqlUtil.dropTable(conn, "t1");
    }

          return result;
      }
      */

    public static String test10() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        String result = "";
        SqlUtil.createTable(conn, "t1", "a int", "b varchar(10)");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        try {
            SqlUtil.insertRow(
                    conn, "t1", new Arg("a", Types.INTEGER, 1), new Arg("b", Types.VARCHAR, "a"));
            Statement stmt =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = stmt.executeQuery("select t1 from t1");
            result += TestUtil.assertEquals(true, rs.next());
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            oid.setValues(new String[] {"a", "b"}, new Object[] {10, "aaaa"});

            rs = stmt.executeQuery("select * from t1");
            result += TestUtil.assertEquals(true, rs.next());
            result += TestUtil.assertEquals(10, rs.getInt("a"));
            result += TestUtil.assertEquals("aaaa", rs.getString("b"));
            try {
                oid.setValues(new String[1], null);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.setValues(null, null);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.setValues(new String[1], new Object[2]);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.addToSet(null, new Object());
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.removeFromSet(null, new Object());
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.addToSequence(null, 0, new Object());
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.putIntoSequence(null, 0, new Object());
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                oid.removeFromSequence(null, 0);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                ((CUBRIDServerSideOID) oid).getNewInstance(null, null);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }
            try {
                ((CUBRIDServerSideOID) oid).getNewInstance(conn, null);
                result += TestUtil.assertTrue(false);
            } catch (Exception e) {
                result += TestUtil.assertTrue(true);
            }

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }

    public static String test11() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        String result = "";
        SqlUtil.createTable(conn, "t1", "a int", "b string");

        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 values (?, ?)");
            stmt.setInt(1, 50);
            stmt.setString(2, "cubrid");
            stmt.execute();
            stmt.close();

            Statement stmt2 =
                    conn.createStatement(
                            ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
            ResultSet rs = stmt2.executeQuery("select t1 from t1");
            rs.next();
            CUBRIDOID oid = ((CUBRIDServerSideResultSet) rs).getOID();
            ResultSet rs2 = oid.getValues(new String[] {"a", "b"});
            rs2.next();
            Integer i = rs2.getInt(1);
            result += TestUtil.assertEquals(50, i);

            String str = rs2.getString(2);
            result += TestUtil.assertEquals("cubrid", str);

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return result;
    }
}
