package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class GetTablesTaskTest extends
		SetupJDBCTestCase {

	public void testGetTablesTask() {		
		GetTablesTask task = new GetTablesTask(
				databaseInfo, site, null, port);
		List<String> list=task.getAllTableAndViews();		
		assertTrue(list.contains("athlete"));
		assertTrue(list.contains("db_attr_setdomain_elm"));
		assertTrue(list.contains("_db_attribute"));
		
		
		
		task = new GetTablesTask(
				databaseInfo, site, null, port);
		list=task.getAllTables();
		assertTrue(list.contains("athlete"));
		assertTrue(list.contains("_db_attribute"));
		assertFalse(list.contains("db_attr_setdomain_elm"));
		
		task = new GetTablesTask(
				databaseInfo, site, null, port);
		list=task.getSystemTables();
		assertTrue(list.contains("_db_attribute"));
		assertFalse(list.contains("athlete"));		
		assertFalse(list.contains("db_attr_setdomain_elm"));
		
		task = new GetTablesTask(
				databaseInfo, site, null, port);
		list=task.getUserTables();		
		assertTrue(list.contains("athlete"));		
		assertFalse(list.contains("_db_attribute"));
		assertFalse(list.contains("db_attr_setdomain_elm"));
		
		
	}
}