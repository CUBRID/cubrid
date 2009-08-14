package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.ArrayList;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;

public class GetAutoIncrementTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetAutoIncrementTaskTest";
	String sql = null;

	private boolean createTestTable() {
		String sql = "create table \"" + testTableName + "\" ("
				+ "auto smallint AUTO_INCREMENT(3,5))";	
		return executeDDL(sql);
	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		return executeDDL(sql);
	}

	public void testGetAutoIncrementTaskTest() {
		createTestTable();
		GetAutoIncrementTask task = new GetAutoIncrementTask(
				databaseInfo, site, databaseInfo.getDriverPath(), port);
		task.setTableName(testTableName);
		task.execute();
		ArrayList<SerialInfo> list = task.getSerialInfoList();
		assertEquals(1, list.size());
		SerialInfo serial=list.get(0);
		assertEquals("3", serial.getMinValue());
		assertEquals("5", serial.getIncrementValue());
		dropTestTable();
	}
}