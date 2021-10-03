package test;

import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import java.io.StringReader;
import java.math.BigDecimal;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Connection;
import java.sql.Date;
import java.sql.DriverManager;
import java.sql.NClob;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.sql.Types;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import test.SqlUtil.Arg;

public class TestCUBRIDResultSet {

    private static final String TABLE = "test_rs";

    private static ResultSet executeQuery(Connection conn, String templ, Object... args)
            throws SQLException {
        return executeQuery(conn, true, templ, args);
    }

    private static ResultSet executeQuery(
            Connection conn, boolean doNext, String templ, Object... args) throws SQLException {
        ResultSet rs = SqlUtil.executeQuery(conn, templ, args);

        if (doNext) {
            rs.next();
        }

        return rs;
    }

    private static String assertTrue(boolean condition) {
        if (condition) {
            return "t ";
        } else {
            return "f ";
        }
    }

    private static String assertArrayEquals(Object[] expected, Object[] actual) {
        if (expected.length != actual.length) {
            return "f ";
        }

        for (int i = 0; i < expected.length; i++) {
            if (expected[i].equals(actual[i])) {
                return "f ";
            }
        }

        return "t ";
    }

    private static String assertArrayEquals(byte[] expected, byte[] actual) {
        if (expected.length != actual.length) {
            return "f ";
        }

        if (!java.util.Arrays.equals(expected, actual)) {
            return "f ";
        }

        return "t ";
    }

    private static String assertEquals(Object expected, Object actual) {
        if ((expected == null) && (actual == null)) {
            return "t ";
        }
        if (expected != null) {
            if (expected.getClass().isArray()) {
                return assertArrayEquals((Object[]) expected, (Object[]) actual);
            } else if (expected.equals(actual)) {
                return "t ";
            }
        }
        return "f ";
    }

    private static String assertEquals(float expected, float actual, float eps) {
        if (Math.abs(expected - actual) < eps) {
            return "t ";
        }

        return "f ";
    }

    private static String assertEquals(double expected, double actual, double eps) {
        if (Math.abs(expected - actual) < eps) {
            return "t ";
        }

        return "f ";
    }

    /* Test getArray() throws UnsupportedOperationException */
    public static String test1_01() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            rs.getArray(1);
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* Test getArray() throws UnsupportedOperationException */
    public static String test1_02() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        try {
            SqlUtil.createTable(conn, TABLE, "a int");
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            rs.getArray("a");
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test decimal */
    public static String test1_04() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a decimal(10, 4)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DECIMAL, "123.456"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DECIMAL, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        BigDecimal val = rs.getBigDecimal("a");
        System.out.println(val.doubleValue());
        // result += assertEquals(123.456, val.doubleValue(),0.001);

        rs.next();
        result += assertEquals(null, rs.getBigDecimal("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test blob */
    public static String test1_05() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        InputStream is = rs.getBinaryStream("a");
        byte[] buff = new byte[256];
        int c = is.read(buff);
        System.out.println(new String(buff, 0, c));
        result += assertEquals(10, c);
        result += assertArrayEquals("0123456789".getBytes(), Arrays.copyOf(buff, c));

        rs.next();
        result += assertEquals(null, rs.getBinaryStream("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test blob */
    public static String test1_06() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        Blob blob = rs.getBlob("a");
        result += assertEquals(10, blob.length());
        byte[] data = blob.getBytes(1, 10);
        result += assertArrayEquals("0123456789".getBytes(), data);

        rs.next();
        result += assertEquals(null, rs.getBlob("a"));
        return result;
    }

    /* test integer */
    public static String test1_07() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, "1"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        boolean val = rs.getBoolean("a");
        result += assertEquals(true, val);

        rs.next();
        result += assertEquals(false, rs.getBoolean("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test integer, getByte */
    public static String test1_08() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, "1"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        byte val = rs.getByte("a");
        result += assertEquals(1, val);

        rs.next();
        result += assertEquals(0, rs.getByte("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test blob */
    public static String test1_09() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        byte[] val = rs.getBytes("a");
        result += assertArrayEquals("0123456789".getBytes(), val);

        rs.next();
        result += assertEquals(null, rs.getBytes("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test blob */
    public static String test1_10() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        byte[] val = rs.getBytes("a");
        result += assertArrayEquals("0123456789".getBytes(), val);

        rs.next();
        result += assertEquals(null, rs.getBytes("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test clob */
    public static String test1_11() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        Reader r = rs.getCharacterStream("a");
        result += assertEquals("0123456789", rs.getString("a"));
        char[] buff = new char[256];
        int c = r.read(buff);

        result += assertEquals(10, c);
        result += assertEquals("0123456789", new String(buff, 0, c));
        rs.next();
        result += assertEquals(null, rs.getCharacterStream("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test clob */
    public static String test1_12() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        Clob clob = rs.getClob("a");
        char[] buff = new char[256];
        result += assertEquals(10, clob.length());
        result += assertEquals("0123456789", clob.getSubString(1, 10));

        rs.next();
        result += assertEquals(null, rs.getClob("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test date */
    public static String test1_14() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a date");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DATE, "2010-01-23"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DATE, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
        result += assertEquals("2010-01-23", sdf.format(rs.getDate("a", null)));
        result += assertEquals("2010-01-23", sdf.format(rs.getDate(1, null)));
        System.out.println(rs.getDate("a"));
        rs.next();
        result += assertEquals(null, rs.getDate("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test time */
    public static String test1_15() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a time");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIME, "11:23:35"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIME, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        SimpleDateFormat sdf = new SimpleDateFormat("hh:mm:ss");
        result += assertEquals("11:23:35", sdf.format(rs.getTime("a", null)));
        result += assertEquals("11:23:35", sdf.format(rs.getTime(1, null)));

        rs.next();
        result += assertEquals(null, rs.getTime("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test datetime */
    public static String test1_16() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a datetime");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIMESTAMP, "2010-03-23 11:23:35.123"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIMESTAMP, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd hh:mm:ss.SSS");
        result += assertEquals("2010-03-23 11:23:35.123", sdf.format(rs.getTimestamp("a", null)));
        result += assertEquals("2010-03-23 11:23:35.123", sdf.format(rs.getTimestamp(1, null)));

        rs.next();
        result += assertEquals(null, rs.getTimestamp("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test double */
    public static String test1_17() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a double");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DOUBLE, "1234.5678"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DOUBLE, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(1234.5678, rs.getDouble("a"), 0.00000001);

        rs.next();
        result += assertEquals(0.0, rs.getDouble("a"), 0);
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test float */
    public static String test1_18() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a float");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.FLOAT, "1234.5678"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.FLOAT, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(1234.5678, rs.getFloat("a"), 0.0001);

        rs.next();
        result += assertEquals(0.0f, rs.getFloat("a"), 0);
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test integer */
    public static String test1_19() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, "1234"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(1234, rs.getInt("a"));

        rs.next();
        result += assertEquals(0, rs.getInt("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test bigint */
    public static String test1_20() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a bigint");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BIGINT, "1234567890123456"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BIGINT, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(1234567890123456L, rs.getLong("a"));

        rs.next();
        result += assertEquals(0, rs.getLong("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test clob */
    public static String test1_21() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "1234567890123456"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            Reader r = rs.getNCharacterStream("a");

            rs.next();
            result += assertEquals(null, rs.getNCharacterStream("a"));
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test clob */
    public static String test1_22() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "1234567890123456"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            NClob clob = rs.getNClob("a");

            rs.next();
            result += assertEquals(null, rs.getNClob("a"));
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test varchar */
    public static String test1_23() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            String val = rs.getNString("a");
            result += assertEquals("0123456789", val);
            rs.next();
            result += assertEquals(null, rs.getNString("a"));
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test varchar */
    public static String test1_24() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        Object val = rs.getObject("a");
        result += assertEquals(String.class, val.getClass());

        rs.next();
        result += assertEquals(null, rs.getObject("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* TODO: OID
    public static String test1_25() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));

        stmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        stmt.close();

        ResultSet rs = executeQuery(conn, "select %s as b, a from %s", TABLE, TABLE);
        CUBRIDOID val = rs.getOID("b");
        result += assertEquals(TABLE, val.getTableName());
        ResultSet rs2 = val.getValues(new String[] {"a"});
        result += assertEquals(true, rs2.next());
        result += assertEquals("0123456789", rs.getString("a"));
        rs.next();
        result += assertEquals(null, rs.getObject("a"));
    }
    */

    /* test varchar */
    public static String test1_26() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            rs.getRef("a");
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test varchar */
    public static String test1_27() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");

        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            rs.getRowId("a");
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test smallint */
    public static String test1_28() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, "12345"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(12345, rs.getShort("a"));

        rs.next();
        result += assertEquals(0, rs.getShort("a"));
        return result;
    }

    /* test smallint */
    public static String test1_29() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, "12345"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            SQLXML sx = rs.getSQLXML("a");
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test varchar */
    public static String test1_30() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "12345"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals("12345", rs.getString("a"));

        rs.next();
        result += assertEquals(null, rs.getString("a"));
        return result;
    }

    /* test clob */
    public static String test1_31() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            InputStream is = rs.getUnicodeStream("a");
            byte[] buff = new byte[256];
            int c = is.read(buff);
            result += assertEquals(20, c);
            result += assertEquals("0123456789".getBytes("UTF-16"), new String(buff, 0, c));
            rs.next();
            result += assertEquals(null, rs.getUnicodeStream("a"));
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test clob */
    public static String test1_31b() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a clob");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, "0123456789"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.CLOB, null));
        try {
            ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
            InputStream is = rs.getUnicodeStream(1);
            byte[] buff = new byte[256];
            int c = is.read(buff);
            result += assertEquals(20, c);
            result += assertEquals("0123456789".getBytes("UTF-16"), new String(buff, 0, c));
            rs.next();
            result += assertEquals(null, rs.getUnicodeStream("a"));
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test varchar */
    public static String test2() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        result += assertEquals(1, rs.getRow());
        result += assertEquals(true, rs.next());
        result += assertEquals(2, rs.getRow());
        result += assertEquals(false, rs.next());
        result += assertEquals(0, rs.getRow());
        return result;
    }

    /* test varchar */
    public static String test3() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, null));

        ResultSet rs = executeQuery(conn, "select a from %s", TABLE);
        rs.clearWarnings();
        result += assertEquals(null, rs.getWarnings());
        result += assertEquals("", rs.getCursorName());
        return result;
    }

    /* TODO: do we need to test it? */
    /*
    public static String test6() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");
        stmt.close();

        CUBRIDConnection conn2 = ConnectionProvider.getConnection();
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        conn2.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        //	conn.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
        //	conn2.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
        conn.setLockTimeout(10000);
        conn2.setLockTimeout(10000);
        result += assertEquals(true, conn.getAutoCommit());
        result += assertEquals(true, conn2.getAutoCommit());
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));

        stmt = conn.createStatement(ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_READ_ONLY);

        stmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");

        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select a from %s", TABLE));
        rs.next();

        try {
            SqlUtil.insertRow(conn2, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        } finally {
            conn2.close();
        }

        if (conn.getMetaData().insertsAreDetected(rs.getType())) {
            result += assertEquals(true, rs.rowInserted());
        } else {
            result += assertEquals(false, rs.rowInserted());
        }
    }
    */

    /* TODO: do we need to test it? */
    /*
    public static String test7() throws SQLException, IOException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
        conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement stmt = conn.createStatement();
        stmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");
        stmt.close();

        CUBRIDConnection conn2 = ConnectionProvider.getConnection();
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        conn2.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        //	conn.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
        //	conn2.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
        conn.setLockTimeout(10000);
        conn2.setLockTimeout(10000);
        result += assertEquals(true, conn.getAutoCommit());
        result += assertEquals(true, conn2.getAutoCommit());
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        conn.close();

        conn = ConnectionProvider.getConnection();
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);

        stmt = conn.createStatement(ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select a from %s", TABLE));
        rs.next();

        rs.deleteRow();

        if (conn.getMetaData().deletesAreDetected(rs.getType())) {
            result += assertEquals(true, rs.rowDeleted());
        } else {
            result += assertEquals(false, rs.rowDeleted());
        }

        return result;
    }
    */

    /* TODO: OID */
    /*
    public static String test8() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(30)");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        CUBRIDOID oid = rs.getOID();
    }
    */

    /* test updateRow */
    public static String test9_01() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a int");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(Integer.valueOf(10), rs.getObject("a"));
        rs.updateNull("a");
        rs.updateRow();
        result += assertEquals(null, rs.getObject("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(null, rs.getObject("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test updaterow */
    public static String test9_03() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a smallint");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(10, rs.getByte("a"));
        rs.updateByte("a", (byte) 20);
        rs.updateRow();
        result += assertEquals(20, rs.getByte("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(20, rs.getByte("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test updaterow */
    public static String test9_04() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a smallint");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.SMALLINT, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(10, rs.getShort("a"));
        rs.updateShort("a", (byte) 20);
        rs.updateRow();
        result += assertEquals(20, rs.getShort("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(20, rs.getShort("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test updaterow */
    public static String test9_05() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a int");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(10, rs.getInt("a"));
        rs.updateInt("a", 20);
        rs.updateRow();
        result += assertEquals(20, rs.getInt("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(20, rs.getInt("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test updaterow */
    public static String test9_06() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        long val1 = 0x4000000000000000L;
        long val2 = 0x8000000000000000L;
        SqlUtil.createTable(conn, TABLE, "a bigint");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.BIGINT, val1));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(val1, rs.getLong("a"));
        rs.updateLong("a", val2);
        rs.updateRow();
        result += assertEquals(val2, rs.getLong("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(val2, rs.getLong("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test updaterow */
    public static String test9_07() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a float");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.FLOAT, 0.123));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(0.123f, rs.getFloat("a"), 0.0001);
        rs.updateFloat("a", 0.321f);
        rs.updateRow();
        result += assertEquals(0.321, rs.getFloat("a"), 0.0001);
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(0.321, rs.getFloat("a"), 0.0001);
        return result;
    }

    /* test updaterow */
    public static String test9_08() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a double");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DOUBLE, 0.123));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(0.123, rs.getDouble("a"), 0.0001);
        rs.updateDouble("a", 0.321);
        rs.updateRow();
        result += assertEquals(0.321, rs.getDouble("a"), 0.0001);
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(0.321, rs.getDouble("a"), 0.0001);
        return result;
    }

    /* test updaterow */
    public static String test9_10() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "1234"));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals("1234", rs.getString("a"));
        rs.updateString("a", "4321");
        rs.updateRow();
        result += assertEquals("4321", rs.getString("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("4321", rs.getString("a"));
        return result;
    }

    /* test updaterow */
    public static String test9_12() throws SQLException, ParseException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a date");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.DATE, "2010-09-03"));

        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
        result += assertEquals("2010-09-03", sdf.format(rs.getDate("a")));
        rs.updateDate("a", new Date(sdf.parse("2010-09-05").getTime()));
        rs.updateRow();
        result += assertEquals("2010-09-05", sdf.format(rs.getDate("a")));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("2010-09-05", sdf.format(rs.getDate("a")));
        return result;
    }

    /* test updaterow */
    public static String test9_13() throws SQLException, ParseException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a time");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIME, "10:10:10"));

        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        SimpleDateFormat sdf = new SimpleDateFormat("hh:mm:ss");
        result += assertEquals("10:10:10", sdf.format(rs.getTime("a")));
        rs.updateTime("a", new Time(sdf.parse("10:10:20").getTime()));
        rs.updateRow();
        result += assertEquals("10:10:20", sdf.format(rs.getTime("a")));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("10:10:20", sdf.format(rs.getTime("a")));
        return result;
    }

    /* test updaterow */
    public static String test9_14() throws SQLException, ParseException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a datetime");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.TIMESTAMP, "2010-09-03 10:10:10"));

        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd hh:mm:ss");
        result += assertEquals("2010-09-03 10:10:10", sdf.format(rs.getTimestamp("a")));
        rs.updateTimestamp("a", new Timestamp(sdf.parse("2010-09-05 10:10:20").getTime()));
        rs.updateRow();
        result += assertEquals("2010-09-05 10:10:20", sdf.format(rs.getTimestamp("a")));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("2010-09-05 10:10:20", sdf.format(rs.getTimestamp("a")));
        return result;
    }

    /* test updaterow */
    public static String test9_18() throws SQLException, ParseException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a varchar(20)");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.VARCHAR, "0123456789"));

        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals("0123456789", rs.getString("a"));
        rs.updateObject("a", "9876543210");
        rs.updateRow();
        result += assertEquals("9876543210", rs.getString("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("9876543210", rs.getString("a"));
        return result;
    }

    /* test updateXX() and cancelRowUpdates() */
    public static String test10() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a int");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(10, rs.getInt("a"));
        rs.updateInt("a", 20);
        rs.cancelRowUpdates();
        result += assertEquals(10, rs.getInt("a"));
        rs.close();
        stmt.close();

        rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(10, rs.getInt("a"));
        return result;
    }

    /* test moveToInsertRow() and moveToCurrentRow() */
    public static String test11() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        Statement preStmt = conn.createStatement();
        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=no';");

        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a int");

        preStmt.executeUpdate("set system parameters 'create_table_reuseoid=yes';");
        preStmt.close();

        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        rs.next();
        result += assertEquals(1, rs.getRow());
        rs.moveToInsertRow();
        // result += assertEquals(0, rs.getRow());
        rs.moveToCurrentRow();
        return result;
    }

    public static String test12() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        conn.setHoldability(ResultSet.CLOSE_CURSORS_AT_COMMIT);
        SqlUtil.createTable(conn, TABLE, "a int");
        SqlUtil.insertRow(conn, TABLE, new Arg("a", Types.INTEGER, 10));
        Statement stmt =
                (Statement)
                        conn.createStatement(
                                ResultSet.TYPE_SCROLL_SENSITIVE, ResultSet.CONCUR_UPDATABLE);
        ResultSet rs = (ResultSet) stmt.executeQuery(String.format("select * from %s", TABLE));
        result += assertTrue(rs.getStatement() == stmt);
        return result;
    }

    public static String test13() throws SQLException {
        String result = "t";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setInt(1, 10);
        stmt.clearParameters();

        try {
            stmt.execute();
            return "f";
        } catch (SQLException e) {
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    public static String test14() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setObject(1, Integer.valueOf(10));
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(10, rs.getInt("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    public static String test15() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a int");
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setObject(1, Integer.valueOf(10), Types.INTEGER, 0);
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals(10, rs.getInt("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    public static String test17() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        Blob blob = conn.createBlob();
        blob.setBytes(1, "1234".getBytes());
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setObject(1, blob, Types.BLOB);
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result +=
                assertArrayEquals(
                        "1234".getBytes(), rs.getBlob("a").getBytes(1, (int) blob.length()));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    public static String test18() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a clob");
        Clob clob = conn.createClob();
        clob.setString(1, "1234");
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setObject(1, clob, Types.CLOB);
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("1234", rs.getClob("a").getSubString(1, (int) clob.length()));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    public static String test19() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob");
        Blob blob = conn.createBlob();
        blob.setBytes(1, "1234".getBytes());
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setObject(1, blob);
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result +=
                assertArrayEquals(
                        "1234".getBytes(), rs.getBlob("a").getBytes(1, (int) blob.length()));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    public static String test20a() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
        stmt.setCharacterStream(1, new StringReader("1234"), 4);
        stmt.execute();

        ResultSet rs = executeQuery(conn, "select * from %s", TABLE);
        result += assertEquals("1234", rs.getString("a"));
        SqlUtil.dropTable(conn, TABLE);
        return result;
    }

    /* test setNCharacterStream throws UnsupportedOperationException (why this test is here?) */
    public static String test21a() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNCharacterStream(1, new StringReader("1234"));
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setNCharacterStream throws UnsupportedOperationException (why this test is here?) */
    public static String test21b() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNCharacterStream(1, new StringReader("1234"), 4L);
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setNClob throws UnsupportedOperationException (why this test is here?) */
    public static String test21c() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNClob(1, new StringReader("1234"));
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setNClob throws UnsupportedOperationException (why this test is here?) */
    public static String test21d() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNClob(1, new StringReader("1234"), 4L);
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setNClob throws UnsupportedOperationException (why this test is here?) */
    public static String test21e() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNClob(1, (NClob) null);
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setNString throws UnsupportedOperationException (why this test is here?) */
    public static String test21f() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setNString(1, "1234");
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setRowId throws UnsupportedOperationException (why this test is here?) */
    public static String test21g() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setRowId(1, (RowId) null);
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /* test setSQLXML throws UnsupportedOperationException (why this test is here?) */
    public static String test21h() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a varchar(10)");
        String sql = String.format("insert into %s values(?)", TABLE);
        try {
            PreparedStatement stmt = (PreparedStatement) conn.prepareStatement(sql);
            stmt.setSQLXML(1, (SQLXML) null);
            stmt.execute();
        } catch (SQLException e) {
            result += assertTrue(e.getCause() instanceof UnsupportedOperationException);
        } finally {
            SqlUtil.dropTable(conn, TABLE);
        }
        return result;
    }

    /*
    public static String test22() throws SQLException {
        String result = "";
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, TABLE, "a blob, b clob");
        ResultSet rs = executeQuery(conn, false, "select * from %s", TABLE);
        ResultSetMetaData md = rs.getMetaData();
        System.out.println(md.getColumnCount());
        System.out.println(md.getColumnTypeName(1));
        System.out.println(md.getColumnTypeName(2));
    }
    */
}
