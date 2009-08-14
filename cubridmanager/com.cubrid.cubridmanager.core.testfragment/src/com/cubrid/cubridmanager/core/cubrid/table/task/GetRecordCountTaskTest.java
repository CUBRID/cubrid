package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class GetRecordCountTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetRecordCountTaskTest";
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

	public void testGetRecordCountTaskTest() {
		createTestTable();
		insertData(10);
		GetRecordCountTask task = new GetRecordCountTask(databaseInfo, site,
				null, port);
		int count = task.getRecordCount(testTableName, null);

		task.execute();
		assertEquals(10, count);
		dropTestTable();

	}
}