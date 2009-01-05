package cubridmanager.query;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

public class QueryUtil {

	public static void freeQuery(Connection conn, Statement stmt, ResultSet rs) {
		try { if (rs != null) rs.close(); rs = null; } catch (Exception ignored) {}
		try { if (stmt != null) stmt.close(); stmt = null; } catch (Exception ignored) {}
		try { if (conn != null) conn.close(); conn = null; } catch (Exception ignored) {}
	}
	
	public static void freeQuery(Connection conn, Statement stmt) {
		try { if (stmt != null) stmt.close(); stmt = null; } catch (Exception ignored) {}
		try { if (conn != null) conn.close(); conn = null; } catch (Exception ignored) {}
	}
	
	public static void freeQuery(Statement stmt, ResultSet rs) {
		try { if (rs != null) rs.close(); rs = null; } catch (Exception ignored) {}
		try { if (stmt != null) stmt.close(); stmt = null; } catch (Exception ignored) {}
	}
	
	public static void freeQuery(Connection conn) {
		try { if (conn != null) conn.close(); conn = null; } catch (Exception ignored) {}
	}
	
	public static void freeQuery(Statement stmt) {
		try { if (stmt != null) stmt.close(); stmt = null; } catch (Exception ignored) {}
	}
	
	public static void freeQuery(ResultSet rs) {
		try { if (rs != null) rs.close(); rs = null; } catch (Exception ignored) {}
	}
	
}
