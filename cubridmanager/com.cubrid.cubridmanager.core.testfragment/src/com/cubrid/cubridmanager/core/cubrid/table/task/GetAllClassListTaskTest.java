package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;

public class GetAllClassListTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetAllClassListTaskTest";
	String sql = null;

	public void testGetAllClassListTaskTest() {

		GetAllClassListTask task = new GetAllClassListTask(databaseInfo, site,
				null, port);
		List<ClassInfo> allClassInfoList = task.getSchema(false, true);
		boolean found = false;
		for (ClassInfo c : allClassInfoList) {
			if (c.getClassName().equals("_db_attribute")) {
				found = true;
				break;
			}
		}
		assertTrue(found);

		task = new GetAllClassListTask(databaseInfo, site, null, port);
		allClassInfoList = task.getSchema(false, false);
		found = false;
		for (ClassInfo c : allClassInfoList) {
			if (c.getClassName().equals("db_attribute")) {
				found = true;
				break;
			}
		}
		assertTrue(found);

		task = new GetAllClassListTask(databaseInfo, site, null, port);
		allClassInfoList = task.getAllClassInfoList();
		found = false;
		for (ClassInfo c : allClassInfoList) {
			if (c.getClassName().equals("db_attribute")) {				
				found = true;
				break;
			}
		}
		assertTrue(found);

		task = new GetAllClassListTask(databaseInfo, site, null, port);
		task.setTableName("Db_attribute");
		task.getClassInfoTaskExcute();
		ClassInfo c = task.getClassInfo();
		found = false;

		if (c.getClassName().equals("db_attribute")) {
			found = true;
		}

		assertTrue(found);

	}
}