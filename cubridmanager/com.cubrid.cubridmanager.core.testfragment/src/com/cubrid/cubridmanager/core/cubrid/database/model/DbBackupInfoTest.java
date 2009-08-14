package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.ArrayList;
import java.util.List;

import junit.framework.TestCase;

public class DbBackupInfoTest extends
		TestCase {

	public void testDbBackupInfo() {
		DbBackupInfo dbBackupInfo = new DbBackupInfo();
		dbBackupInfo.setDbDir("/home/daniel");
		dbBackupInfo.setFreeSpace("100");
		List<DbBackupHistoryInfo> list = new ArrayList<DbBackupHistoryInfo>();
		list.add(new DbBackupHistoryInfo("1", "/home/daniel", "100",
				"2009/10/12"));
		dbBackupInfo.setBackupHistoryList(list);
		dbBackupInfo.addDbBackupHistoryInfo(new DbBackupHistoryInfo("1", "/home/daniel", "100",
				"2009/10/12"));
		assertEquals(dbBackupInfo.getDbDir(), "/home/daniel");
		assertEquals(dbBackupInfo.getFreeSpace(), "100");
		assertTrue(dbBackupInfo.getBackupHistoryList().size() == 2);
	}
}
