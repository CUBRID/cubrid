package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * This task is responsible to get triggers by database user name
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-10 created by pangqiren
 */
public class GetTriggerListByUserNameTask extends
		JDBCTask {

	/**
	 * The constructor
	 * 
	 * @param dbInfo
	 * @param serverInfo
	 * @param driverPath
	 * @param brokerPort
	 */
	public GetTriggerListByUserNameTask(DatabaseInfo dbInfo) {
		super("GetTriggerListByUserName", dbInfo, false);
	}

	/**
	 * 
	 * Get trigger list
	 * 
	 * @param dbUserName
	 * @return
	 */
	public List<String> getTriggerList(String dbUserName) {
		List<String> triggerList = new ArrayList<String>();
		try {
			if (dbUserName == null || errorMsg != null
					&& errorMsg.trim().length() > 0) {
				return triggerList;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return triggerList;
			}
			String sql = "select name from db_trigger where UPPER(owner.name)=UPPER('"
					+ dbUserName + "')";
			stmt = connection.prepareStatement(sql);
			rs = ((PreparedStatement) stmt).executeQuery();
			while (rs.next()) {
				String name = rs.getString("name");
				if (name != null) {
					triggerList.add(name);
				}
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return triggerList;
	}

}
