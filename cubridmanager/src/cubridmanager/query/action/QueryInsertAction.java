package cubridmanager.query.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.SQLException;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class QueryInsertAction {
	public static String resultMsg = new String();

	public static boolean insertRecord(String dbName, String sql) {
		Connection con = null;
		PreparedStatement stmt = null;
		QueryEditorConnection connector;
		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			connector = new QueryEditorConnection(MainRegistry
					.getDBUserInfo(dbName));
			con = DriverManager.getConnection(connector.getConnectionStr());
			if (MainRegistry.isProtegoBuild()) {
				CUBRIDConnectionKey conKey = ((CUBRIDConnection) con)
						.Login(MainRegistry.UserSignedData);
				CUBRIDKeyTable.putValue(conKey);
			}
			con.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
			((CUBRIDConnection) con).setLockTimeout(1);
			stmt = con.prepareStatement(sql);
			stmt.execute();

			resultMsg = stmt.getUpdateCount()
					+ Messages.getString("QEDIT.INSERTOK");

			stmt.close();
			if (MainRegistry.isProtegoBuild()) {
				((CUBRIDConnection) con).Logout();
			}
			con.close();

			return true;
		} catch (SQLException e) {
			resultMsg = Messages.getString("QEDIT.INSERTFAIL")
					+ e.getErrorCode() + " - " + e.getMessage();
			CommonTool.debugPrint(e);
			return false;
		} catch (ClassNotFoundException e) {
			resultMsg = Messages.getString("QEDIT.INSERTFAIL")
					+ Messages.getString("QEDIT.SETCONNERR") + " - "
					+ e.getMessage();
			CommonTool.debugPrint(e);
			return false;
		}
	}
}
