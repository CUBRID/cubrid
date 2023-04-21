import java.sql.*;
import java.util.*;

public class SpTest {
    public static String f1(String a1) throws Exception {
		Connection conn = null;
		Statement stmt = null;
		ResultSet rs = null;
		String sql = null;

		String res = "";

		try {
            sql = "SELECT cc1 FROM tbl_c WHERE cc2 = " + a1 + ";";
			conn = DriverManager.getConnection("jdbc:default:connection:");
			stmt = conn.createStatement();
			rs = stmt.executeQuery(sql);

			if (rs.next()) {
				res = rs.getString("cc1");
			}

			rs.close();
			stmt.close();
			conn.close();

			return res;
		} catch (Exception e) {
			if (rs != null)
				rs.close();
			if (stmt != null)
				stmt.close();
			if (conn != null)
				conn.close();

			e.printStackTrace();

			return res;
		}
	}

    public static String f2(String a1, String a2) throws Exception {
		Connection conn = null;
		Statement stmt = null;
		ResultSet rs = null;
		String sql = null;

		String res = "";

		try {
            sql = "SELECT bc1 FROM tbl_b WHERE bc2 = " + a1 + " and " + "bc3 = " + a2 + ";";
			conn = DriverManager.getConnection("jdbc:default:connection:");
			stmt = conn.createStatement();
			rs = stmt.executeQuery(sql);

			if (rs.next()) {
				res = rs.getString("bc1");
			}

			rs.close();
			stmt.close();
			conn.close();

			return res;
		} catch (Exception e) {
			if (rs != null)
				rs.close();
			if (stmt != null)
				stmt.close();
			if (conn != null)
				conn.close();

			e.printStackTrace();

			return res;
		}
	}
}