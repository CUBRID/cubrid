package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;

public class GetAllAttrTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetAllAttrTaskTest";
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


	public void testGetAllAttrTaskTest() {
		boolean success = createTestTable();
		System.out.println("pre GetAllClassListTaskTest " + success);

		GetAllAttrTask task = new GetAllAttrTask(databaseInfo, site, null, port);
		List<String> list = task.getAllAttrList(testTableName);
		assertTrue(list.contains("code"));
		assertTrue(list.contains("name"));
		assertTrue(list.contains("gender"));
		assertTrue(list.contains("nation_code"));
		
		task = new GetAllAttrTask(databaseInfo, site, null, port);
		task.setClassName(testTableName);
		task.getDbAllAttrListTaskExcute();
		List<DBAttribute> attrlist = task.getAllAttrList();
		assertEquals(4, attrlist.size());

		dropTestTable();
		System.out.println("post GetAllClassListTaskTest success");

	}
}
