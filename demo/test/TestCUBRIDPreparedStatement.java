package test;

import java.io.ByteArrayInputStream;
import java.io.StringReader;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;

/*
 * CREATE TABLE test ( val INT );
 * CREATE OR REPLACE FUNCTION TC_PS1() RETURN STRING as language java name 'TestCUBRIDPreparedStatement.test1() return java.lang.String';
 */
public class TestCUBRIDPreparedStatement {

    public static String test1() {
        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
            PreparedStatement stmt = conn.prepareStatement("select * from code");
            ResultSet rs = stmt.executeQuery();
            ResultSetMetaData md = stmt.getMetaData();
        } catch (Exception e) {
            return "f";
        }
        return "t";
    }

    public static String test2() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");

        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 (a) values (?)");
            stmt.setInt(1, 10);
            stmt.executeUpdate();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test3() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a clob");

        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 (a) values (?)");
            stmt.setClob(1, new StringReader("0123456789"), 5);
            stmt.executeUpdate();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test4() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a blob");

        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 (a) values (?)");
            stmt.setBlob(1, new ByteArrayInputStream("0123456789".getBytes()), 5);
            stmt.executeUpdate();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test5() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");

        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 (a) values (?)");
            stmt.setInt(1, 10);
            stmt.addBatch();
            stmt.setInt(1, 20);
            stmt.addBatch();
            stmt.clearBatch();
        } catch (Exception e) {
            return "f";
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test6d() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a blob");
        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 values(?)");
            stmt.setAsciiStream(1, null, 10);
            stmt.execute();
            ResultSet rs = SqlUtil.executeQuery(conn, "select * from t1");
            rs.next();

            if (null != rs.getObject("a")) {
                return "f";
            }

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }

        return "t";
    }

    public static String test7d() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a blob");
        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 values(?)");
            stmt.setBinaryStream(1, null, 10);
            stmt.execute();
            ResultSet rs = SqlUtil.executeQuery(conn, "select * from t1");
            rs.next();

            if (null != rs.getObject("a")) {
                return "f";
            }

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test8() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a int");
        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 values(?)");
            stmt.setByte(1, (byte) 10);
            stmt.execute();
            ResultSet rs = SqlUtil.executeQuery(conn, "select * from t1");
            rs.next();
            if ((byte) 10 == (byte) rs.getObject("a")) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test9() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a bit(32)");
        try {
            PreparedStatement stmt = conn.prepareStatement("insert into t1 values(?)");
            stmt.setBytes(1, "abcd".getBytes());
            stmt.execute();
            ResultSet rs = SqlUtil.executeQuery(conn, "select * from t1");
            rs.next();

            if (!"abcd".getBytes().equals(rs.getBytes("a"))) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test10() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a bit(8)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();

            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test11() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a bit varying(8)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();

            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test12() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a datetime");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();

            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test13() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a set(int)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();

            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test14() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a multiset(int)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();

            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15a() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(int)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15b() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(char)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15c() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(varchar)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15d() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(bit(8))");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15e() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(bit(32))");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15f() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(bit varying)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15g() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(short)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15h() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(bigint)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15i() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(float)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15j() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(double)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15k() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(numeric)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15l() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(monetary)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15m() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(date)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15n() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(time)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15o() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(timestamp)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15p() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(datetime)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15q() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(varchar)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    public static String test15r() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a sequence(nchar)");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    // TODO: md.getElementType is not a standard
    /*
    public static String test16a() throws SQLException {
    	Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	SqlUtil.createTable(conn, "t1", "a int");
    	try {
    		PreparedStatement stmt = conn
    		.prepareStatement("select * from t1");
    		ResultSetMetaData md = stmt
    				.getMetaData();
    		if (md == null)
    		{
    			return "f";
    		}
    		md.getElementType(1);
    		} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}
                   return "t";
    }
    */

    // TODO: md.getElementType is not a standard
    /*
    public static String test16b() throws SQLException {
    	Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	SqlUtil.createTable(conn, "t1", "a set(int)");
    	try {
    		PreparedStatement stmt = conn
    		.prepareStatement("select * from t1");
    		ResultSetMetaData md = stmt
    				.getMetaData();
    		if (md == null)
    		{
    			return "f";
    		}

    		if (Types.INTEGER != md.getElementType(1))
    		{
    			return "f";
    		}
     		} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}
                   return "t";
    }
    */

    // TODO: md.getElementType is not a standard
    /*
    public static String test16c() throws SQLException {
    	Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	SqlUtil.createTable(conn, "t1", "a char");
    	try {
    		PreparedStatement stmt = conn
    		.prepareStatement("select * from t1");
    		ResultSetMetaData md = stmt
    				.getMetaData();
    		if (md == null)
    		{
    			return "f";
    		}
    		try {
    			md.getElementType(1);
    			return "f";
    		} catch (Exception e) {
    		}
    		} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}
                   return "t";
    }
    */

    public static String test16d() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        SqlUtil.createTable(conn, "t1", "a char");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
            try {
                md.isWrapperFor(null);
                return "f";
            } catch (Exception e) {
            }
            try {
                md.unwrap(null);
                return "f";
            } catch (Exception e) {
            }

        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }

    // TODO: md.getElementTypeName is not a standard
    /*
    public static String test17a() throws SQLException {
    	Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	SqlUtil.createTable(conn, "t1", "a int");
    	try {
    		PreparedStatement stmt = conn
    		.prepareStatement("select * from t1");
    		ResultSetMetaData md = stmt
    				.getMetaData();
    		if (md == null)
    		{
    			return "f";
    		}
    		md.getElementTypeName(1);
    		} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}
                   return "t";
    }
    */

    // TODO: md.getElementTypeName is not a standard
    /*
    public static String test17b() throws SQLException {
    	Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
    	SqlUtil.createTable(conn, "t1", "a set(int)");
    	try {
    		PreparedStatement stmt = conn
    		.prepareStatement("select * from t1");
    		ResultSetMetaData md = stmt
    				.getMetaData();
    		if (md == null)
    		{
    			return "f";
    		}
    		Assert.assertEquals("INTEGER", md.getElementTypeName(1));
    		} finally {
    		SqlUtil.dropTable(conn, "t1");
    	}
                   return "t";
    }
    */

    public static String test18() throws SQLException {
        Connection conn = DriverManager.getConnection("jdbc:default:connection:", "", "");
        // SqlUtil.createTable(conn, "t1", "int"); // semantic error case
        SqlUtil.createTable(conn, "t1", "a int");
        try {
            PreparedStatement stmt = conn.prepareStatement("select * from t1");
            ResultSetMetaData md = stmt.getMetaData();
            if (md == null) {
                return "f";
            }
            md.getColumnType(0);
        } finally {
            SqlUtil.dropTable(conn, "t1");
        }
        return "t";
    }
}
