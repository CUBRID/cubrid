package com.cubrid.cubridmanager.core.cubrid.database.model;

import junit.framework.TestCase;

public class DbBackupHistoryInfoTest extends
		TestCase {

	public void testDbBackupHistoryInfo() {
		DbBackupHistoryInfo dbBackupHistoryInfo = new DbBackupHistoryInfo("1",
				"/home/daniel", "100", "2009/10/12");
		assertEquals(dbBackupHistoryInfo.getLevel(), "1");
		assertEquals(dbBackupHistoryInfo.getPath(), "/home/daniel");
		assertEquals(dbBackupHistoryInfo.getSize(), "100");
		assertEquals(dbBackupHistoryInfo.getDate(), "2009/10/12");
		dbBackupHistoryInfo.setLevel("2");
		dbBackupHistoryInfo.setPath("/home/daniel/123");
		dbBackupHistoryInfo.setSize("200");
		dbBackupHistoryInfo.setDate("2009/11/20");
		assertEquals(dbBackupHistoryInfo.getLevel(), "2");
		assertEquals(dbBackupHistoryInfo.getPath(), "/home/daniel/123");
		assertEquals(dbBackupHistoryInfo.getSize(), "200");
		assertEquals(dbBackupHistoryInfo.getDate(), "2009/11/20");
	}
}
