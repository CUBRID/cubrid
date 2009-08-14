package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class RenameTableOrViewTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testRenameTableOrViewTaskTest";
	String newTestTableName = "testNewRenameTableOrViewTaskTest";
	String sql = null;

	private boolean createTestTable() {
		String sql = "create table \"" + testTableName + "\" ("
				+ "code integer," + "name character varying(40)  NOT NULL,"
				+ "gender character(1) ," + "nation_code character(3) " + ")";
		return executeDDL(sql);
	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		return executeDDL(sql);
	}


	public void testGetRecordCountTaskTest() {
		createTestTable();		
		RenameTableOrViewTask task = new RenameTableOrViewTask(databaseInfo, site,
				null, port);
		task.setOldClassName(testTableName);
		task.setNewClassName(newTestTableName);
		task.setTable(true);
		task.execute();
		assertEquals(true, task.isSuccess());
		
		task = new RenameTableOrViewTask(databaseInfo, site,
				null, port);
		task.setOldClassName(newTestTableName);
		task.setNewClassName(testTableName);
		task.setTable(true);
		task.execute();
		assertEquals(true, task.isSuccess());
		dropTestTable();

	}
}