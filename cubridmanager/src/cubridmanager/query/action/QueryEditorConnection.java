package cubridmanager.query.action;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.DBUserInfo;

public class QueryEditorConnection {
	private String host;
	private String port;
	private String conStr;
	private Connection conn;
	private DBUserInfo ui = null;
	private static final String NEW_LINE = System.getProperty("line.separator");

	public QueryEditorConnection(DBUserInfo userinfo) throws SQLException,
			ClassNotFoundException {
		host = MainRegistry.HostAddr;
		port = Integer.toString(MainRegistry.queryEditorOption.casport);
		ui = userinfo;

		if (ui != null) {
			if (MainRegistry.isProtegoBuild())
				conStr = "jdbc:cubrid:" + host + ":" + port + ":" + ui.dbname
						+ ":::";
			// + ":" + ":";
			else
				conStr = "jdbc:cubrid:" + host + ":" + port + ":" + ui.dbname
						+ ":" + ui.dbuser + ":" + ui.dbpassword + ":";
		}

		if (MainRegistry.queryEditorOption.charset != null)
			conStr += "charset=" + MainRegistry.queryEditorOption.charset;

		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			conn = DriverManager.getConnection(conStr);
		} catch (SQLException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ NEW_LINE + e.getErrorCode() + NEW_LINE + e.getMessage());
			throw e;
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ NEW_LINE + e.getMessage());
			throw e;
		} finally {
			if (conn != null) {
				conn.close();
			}
		}
	}

	public String getConnectionStr() {
		return conStr;
	}

	public String getDBName() {
		return ui.dbname;
	}

	public String getUserName() {
		return ui.dbuser;
	}
}
