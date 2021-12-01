package test;

import java.io.ByteArrayInputStream;
import java.io.CharArrayReader;
import java.io.InputStream;
import java.io.Reader;
import java.io.StringReader;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.sql.Connection;
import java.sql.Date;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.sql.Types;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.List;

public class SqlUtil {

    public static class Arg {
        public final String name;
        public final int type;
        public final Object value;

        public Arg(String name, int type, Object value) {
            this.name = name;
            this.type = type;
            this.value = value;
        }
    }

    public static Arg arg(String name, int type, Object value) {
        return new Arg(name, type, value);
    }

    public static Connection connect(String url, String userId, String password) throws Exception {
        Connection conn = null;
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection(url, userId, password);
        } catch (Exception e) {
            //
        }
        return conn;
    }

    public static Connection connectServerSide() throws Exception {
        return connect("jdbc:default:connection:", "", "");
    }

    public static void createTable(Connection conn, String table, String... cols)
            throws SQLException {
        try {
            String sql = String.format("drop table if exists %s", table);
            Statement stmt = conn.createStatement();

            try {
                stmt.execute(sql);
            } finally {
                stmt.close();
            }
        } finally {
        }

        String sql = String.format("create table %s", table);
        if (cols.length > 0) {
            sql += " (\n";

            for (String col : cols) {
                sql += "    " + col + ",\n";
            }

            sql = sql.substring(0, sql.length() - 2);
            sql += "\n)\n";
        }

        Statement stmt = conn.createStatement();

        try {
            stmt.execute(sql);
        } finally {
            stmt.close();
        }
    }

    public static void createTable2(Connection conn, String table, String... cols)
            throws SQLException {
        ResultSet rs = conn.getMetaData().getTables(null, null, table, null);
        try {
            if (rs.next()) {
                String sql = String.format("drop table if exists %s", table);
                Statement stmt = conn.createStatement();

                try {
                    stmt.execute(sql);
                } finally {
                    stmt.close();
                }
            }
        } finally {
            rs.close();
        }

        String sql = String.format("create table %s", table);
        if (cols.length > 0) {
            sql += " (\n";

            for (String col : cols) {
                sql += "    " + col + ",\n";
            }

            sql = sql.substring(0, sql.length() - 2);
            sql += "\n)\n";
        }

        Statement stmt = conn.createStatement();

        try {
            stmt.execute(sql);
        } finally {
            stmt.close();
        }
    }

    public static void dropTable(Connection conn, String table) throws SQLException {
        String sql = String.format("drop table if exists %s", table);
        Statement stmt = conn.createStatement();

        try {
            stmt.execute(sql);
        } finally {
            stmt.close();
        }
    }

    public static void createIndex(Connection conn, String table, boolean unique, String... columns)
            throws SQLException {
        StringBuilder sb = new StringBuilder();
        sb.append("create ");

        if (unique) {
            sb.append("unique ");
        }

        sb.append("index ")
                .append(table)
                .append('_')
                .append(columns[0])
                .append(" on ")
                .append(table)
                .append('(');

        for (int i = 0; i < columns.length; i++) {
            if (i > 0) {
                sb.append(',');
            }

            sb.append(columns[i]);
        }

        sb.append(')');
        executeSql(conn, sb.toString());
    }

    public static List<Object[]> queryAsList(Connection conn, String templ, Object... args)
            throws SQLException {
        Statement stmt = conn.createStatement();

        try {
            ResultSet rs = stmt.executeQuery(String.format(templ, args));

            try {
                List<Object[]> items = new ArrayList<Object[]>();

                while (rs.next()) {
                    int columnCount = rs.getMetaData().getColumnCount();
                    Object[] vals = new Object[columnCount];

                    for (int i = 0; i < columnCount; i++) {
                        vals[i] = rs.getObject(i + 1);
                    }

                    items.add(vals);
                }

                return items;

            } finally {
                rs.close();
            }

        } finally {
            stmt.close();
        }
    }

    public static int getCount(Connection conn, String table, String where) throws SQLException {
        Statement stmt = conn.createStatement();

        try {
            String sql = String.format("select count(*) from %s", table);

            if (where != null) {
                sql += String.format(" where %s", where);
            }

            ResultSet rs = stmt.executeQuery(sql);
            rs.next();

            try {
                return rs.getInt(1);
            } finally {
                rs.close();
            }

        } finally {
            stmt.close();
        }
    }

    public static void executeSql(Connection conn, String templ, Object... args)
            throws SQLException {
        Statement stmt = conn.createStatement();

        try {
            stmt.execute(String.format(templ, args));
        } finally {
            stmt.close();
        }
    }

    public static ResultSet executeQuery(Connection conn, String templ, Object... args)
            throws SQLException {
        Statement stmt = conn.createStatement();
        try {
            return stmt.executeQuery(String.format(templ, args));
        } catch (SQLException e) {
            stmt.close();
            throw e;
        } catch (RuntimeException e) {
            stmt.close();
            throw e;
        }
    }

    public static ResultSet executeQuery(
            Connection conn, boolean doNext, String templ, Object... args) throws SQLException {

        ResultSet rs = SqlUtil.executeQuery(conn, templ, args);

        if (doNext) {
            rs.next();
        }

        return rs;
    }

    public static void insertRow(Connection conn, String table, Arg... args) throws SQLException {
        StringBuilder sb1 = new StringBuilder();
        StringBuilder sb2 = new StringBuilder();

        for (Arg arg : args) {
            if (sb1.length() > 0) {
                sb1.append(", ");
            }

            if (sb2.length() > 0) {
                sb2.append(", ");
            }

            sb1.append(arg.name);
            sb2.append("?");
        }

        String sql = String.format("insert into %s (%s) values (%s)", table, sb1, sb2);
        PreparedStatement stmt = conn.prepareStatement(sql);
        int index = 0;

        for (Arg arg : args) {
            index++;
            int type = arg.type;
            Object val = arg.value;

            if (val == null) {
                stmt.setNull(index, type);
                continue;
            }

            switch (type) {
                case Types.ARRAY:
                    throw new IllegalStateException();
                case Types.BIGINT:
                    stmt.setLong(index, toLong(val));
                    break;
                case Types.BINARY:
                    stmt.setBlob(index, toBinary(val));
                    break;
                case Types.BIT:
                    stmt.setBytes(index, toBytes(val));
                    break;
                case Types.BLOB:
                    stmt.setBlob(index, toBinary(val));
                    break;
                case Types.BOOLEAN:
                    stmt.setBoolean(index, toBoolean(val));
                    break;
                case Types.CHAR:
                    stmt.setString(index, toString(val));
                    break;
                case Types.CLOB:
                    stmt.setClob(index, toChars(val));
                    break;
                case Types.DATALINK:
                    throw new IllegalStateException();
                case Types.DATE:
                    stmt.setDate(index, toDate(val));
                    break;
                case Types.DECIMAL:
                    stmt.setBigDecimal(index, toDecimal(val));
                    break;
                case Types.DISTINCT:
                    throw new IllegalStateException();
                case Types.DOUBLE:
                    stmt.setDouble(index, toDouble(val));
                    break;
                case Types.FLOAT:
                    stmt.setFloat(index, (float) toDouble(val));
                    break;
                case Types.INTEGER:
                    stmt.setInt(index, (int) toLong(val));
                    break;
                case Types.JAVA_OBJECT:
                    throw new IllegalArgumentException();
                case Types.LONGNVARCHAR:
                    stmt.setNString(index, toString(val));
                    break;
                case Types.LONGVARBINARY:
                    stmt.setBlob(index, toBinary(val));
                    break;
                case Types.LONGVARCHAR:
                    stmt.setString(index, toString(val));
                    break;
                case Types.NCHAR:
                    stmt.setNString(index, toString(val));
                    break;
                case Types.NCLOB:
                    stmt.setNClob(index, toChars(val));
                    break;
                case Types.NUMERIC:
                    stmt.setBigDecimal(index, toDecimal(val));
                    break;
                case Types.NVARCHAR:
                    stmt.setNString(index, toString(val));
                    break;
                case Types.REAL:
                    stmt.setFloat(index, (float) toDouble(val));
                    break;
                case Types.REF:
                    throw new IllegalArgumentException();
                case Types.ROWID:
                    throw new IllegalArgumentException();
                case Types.SMALLINT:
                    stmt.setShort(index, (short) toLong(val));
                    break;
                case Types.SQLXML:
                    throw new IllegalArgumentException();
                case Types.STRUCT:
                    throw new IllegalArgumentException();
                case Types.TIME:
                    stmt.setTime(index, toTime(val));
                    break;
                case Types.TIMESTAMP:
                    stmt.setTimestamp(index, toTimestamp(val));
                    break;
                case Types.TINYINT:
                    stmt.setByte(index, (byte) toLong(val));
                    break;
                case Types.VARBINARY:
                    stmt.setBlob(index, toBinary(val));
                    break;
                case Types.VARCHAR:
                    stmt.setString(index, toString(val));
                    break;
            }
        }

        stmt.execute();
    }

    private static Date toDate(Object val) {
        if (val instanceof Long) {
            return new Date((Long) val);
        }

        if (val instanceof String) {
            String str = (String) val;
            String format;

            if (str.matches("\\d{4}\\-\\d{1,2}\\-\\d{1,2}( \\d{2}\\:\\d{2}\\:\\d{2})?")) {
                format = "yyyy-MM-dd";
            } else if (str.matches("\\d{2}\\-\\d{1,2}\\-\\d{1,2}( \\d{2}\\:\\d{2}\\:\\d{2})?")) {
                format = "yy-MM-dd";
            } else if (str.matches("\\d{4}/\\d{1,2}+/\\d{1,2}( \\d{2}\\:\\d{2}\\:\\d{2})?")) {
                format = "yyyy/MM/dd";
            } else if (str.matches("\\d{2}/\\d{1,2}+/\\d{1,2}( \\d{2}\\:\\d{2}\\:\\d{2})?")) {
                format = "yy/MM/dd";
            } else if (str.matches("\\d{8}")) {
                format = "yyyyMMdd";
            } else if (str.matches("\\d{6}")) {
                format = "yyMMdd";
            } else {
                throw new IllegalArgumentException("date format error");
            }

            SimpleDateFormat sdf = new SimpleDateFormat(format);

            try {
                return new Date(sdf.parse(str).getTime());
            } catch (ParseException e) {
                throw new IllegalArgumentException(e);
            }
        }

        throw new IllegalArgumentException();
    }

    private static Timestamp toTimestamp(Object val) {
        if (val instanceof Long) {
            return new Timestamp((Long) val);
        }

        if (val instanceof String) {
            String str = (String) val;
            String format;

            if (str.matches("\\d{4}\\-\\d{1,2}\\-\\d{1,2} \\d{2}\\:\\d{2}\\:\\d{2}")) {
                format = "yyyy-MM-dd hh:mm:ss";
            } else if (str.matches(
                    "\\d{4}\\-\\d{1,2}\\-\\d{1,2} \\d{2}\\:\\d{2}\\:\\d{2}\\.\\d{1,3}")) {
                format = "yyyy-MM-dd hh:mm:ss.SSS";
            } else {
                throw new IllegalArgumentException("format error");
            }

            SimpleDateFormat sdf = new SimpleDateFormat(format);

            try {
                return new Timestamp(sdf.parse(str).getTime());
            } catch (ParseException e) {
                throw new IllegalArgumentException(e);
            }
        }

        throw new IllegalArgumentException();
    }

    private static Time toTime(Object val) {
        // System.out.println("toTime --- " + val);
        if (val instanceof Long) {
            return new Time((Long) val);
        }

        if (val instanceof String) {
            String str = (String) val;
            String format;

            if (str.matches("\\d{2}\\:\\d{2}\\:\\d{2}")) {
                format = "hh:mm:ss";
            } else if (str.matches("\\d{2}\\.\\d{2}\\.\\d{2}")) {
                format = "hh.mm.ss";
            } else if (str.matches("\\d{6}")) {
                format = "hhmmss";
            } else {
                throw new IllegalArgumentException("format error");
            }

            SimpleDateFormat sdf = new SimpleDateFormat(format);

            try {
                return new Time(sdf.parse(str).getTime());
            } catch (ParseException e) {
                throw new IllegalArgumentException(e);
            }
        }

        throw new IllegalArgumentException();
    }

    private static BigDecimal toDecimal(Object val) {
        if (val instanceof BigDecimal) {
            return (BigDecimal) val;
        }

        if (val instanceof Double || val instanceof Float) {
            return new BigDecimal(((Number) val).doubleValue());
        }

        if (val instanceof BigInteger) {
            return new BigDecimal((BigInteger) val);
        }

        if (val instanceof Byte
                || val instanceof Short
                || val instanceof Integer
                || val instanceof Long) {
            return new BigDecimal(((Number) val).longValue());
        }

        if (val instanceof CharSequence) {
            return new BigDecimal(val.toString());
        }

        throw new IllegalArgumentException();
    }

    private static long toLong(Object val) {
        if (val instanceof Number) {
            return ((Number) val).longValue();
        }

        if (val instanceof CharSequence) {
            return Long.parseLong(val.toString());
        }

        throw new IllegalArgumentException();
    }

    private static double toDouble(Object val) {
        if (val instanceof Number) {
            return ((Number) val).doubleValue();
        }

        if (val instanceof CharSequence) {
            return Double.parseDouble(val.toString());
        }

        throw new IllegalArgumentException();
    }

    private static InputStream toBinary(Object val) {
        if (val instanceof InputStream) {
            return (InputStream) val;
        }

        if (val instanceof CharSequence) {
            return new ByteArrayInputStream(val.toString().getBytes());
        }

        if (val instanceof byte[]) {
            return new ByteArrayInputStream((byte[]) val);
        }

        throw new IllegalArgumentException();
    }

    private static Reader toChars(Object val) {
        if (val instanceof Reader) {
            return (Reader) val;
        }

        if (val instanceof CharSequence || val instanceof Number || val instanceof Boolean) {
            return new StringReader(val.toString());
        }

        if (val instanceof char[]) {
            return new CharArrayReader((char[]) val);
        }

        throw new IllegalArgumentException();
    }

    private static byte[] toBytes(Object val) {
        if (val instanceof byte[]) {
            return (byte[]) val;
        }

        if (val instanceof CharSequence) {
            return val.toString().getBytes();
        }

        throw new IllegalArgumentException();
    }

    private static boolean toBoolean(Object val) {
        if (val instanceof Boolean) {
            return (Boolean) val;
        }

        if (val instanceof Number) {
            return ((Number) val).doubleValue() != 0;
        }

        if (val instanceof CharSequence) {
            return Boolean.parseBoolean(val.toString());
        }

        throw new IllegalArgumentException();
    }

    private static String toString(Object val) {
        if (val instanceof Number) {
            return val.toString();
        }

        if (val instanceof Boolean) {
            return val.toString();
        }

        if (val instanceof CharSequence) {
            return val.toString();
        }

        throw new IllegalArgumentException();
    }
}
