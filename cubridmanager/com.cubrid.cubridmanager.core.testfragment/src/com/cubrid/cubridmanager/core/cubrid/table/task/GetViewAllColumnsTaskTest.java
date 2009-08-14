package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class GetViewAllColumnsTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetViewAllColumnsTaskTest";
	String sql = null;

	public void testGetAllDBVclassTaskTest() {

		GetViewAllColumnsTask task = new GetViewAllColumnsTask(databaseInfo,
				site, null, port);
		task.setClassName("db_attribute");
		task.getAllVclassListTaskExcute();
		List<String> allVclassList = task.getAllVclassList();
		assert (allVclassList.contains("attr_name"));

	}
}