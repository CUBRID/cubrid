package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class DelAllRecordsTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testDelAllRecordsTask";
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

	private boolean insertData(int count) {
		String sql = "insert into \"" + testTableName + "\" "
				+ "(\"code\", \"name\", \"gender\", \"nation_code\") "
				+ "values (12, 'test', '1', '2')";
		for (int i = 0; i < count; i++) {
			int update = executeUpdate(sql);
			if (update == -1) {
				return false;
			}
		}
		return true;
	}

	public void testDelAllRecordsTask() {
		boolean success = createTestTable();
		System.out.println("DelAllRecords " + success);

		DelAllRecordsTask task = new DelAllRecordsTask(databaseInfo, site,
				null, port);
		task.setTableName(testTableName);
		task.setWarningMsg(null);
		task.execute();
		assertEquals(0, task.getDeleteRecordsCount());

		insertData(10);

		task = new DelAllRecordsTask(databaseInfo, site, null, port);
		task.setTableName(testTableName);
		task.setWhereCondition("where code=12");
		task.execute();
		assertEquals(10, task.getDeleteRecordsCount());

		dropTestTable();
		System.out.println("post DelAllRecords success");

	}

}
