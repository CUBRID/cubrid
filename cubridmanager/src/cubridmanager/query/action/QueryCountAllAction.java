package cubridmanager.query.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.SQLException;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class QueryCountAllAction {
	private static final String NEW_LINE = System.getProperty("line.separator");

	public static void selectCountAll(String dbName, String tableName) {
		Connection con = null;
		PreparedStatement stmt = null;
		CUBRIDResultSet rs = null;
		String sql = "select count(*) from " + tableName;
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
			rs = (CUBRIDResultSet) stmt.getResultSet();
			rs.next();
			CommonTool.InformationBox(Messages
					.getString("TOOL.TABLESELECTCOUNTACTION"), tableName + ": "
					+ rs.getInt(1) + Messages.getString("QEDIT.COUNTALL"));

			rs.close();
			stmt.close();
			if (MainRegistry.isProtegoBuild()) {
				((CUBRIDConnection) con).Logout();
			}

			con.close();
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getErrorCode() + NEW_LINE + e.getMessage());
			CommonTool.debugPrint(e);
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ NEW_LINE + e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

}
