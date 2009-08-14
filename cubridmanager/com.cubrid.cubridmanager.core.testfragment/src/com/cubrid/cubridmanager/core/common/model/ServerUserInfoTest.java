package com.cubrid.cubridmanager.core.common.model;

import java.util.ArrayList;
import java.util.List;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbCreateAuthType;

public class ServerUserInfoTest extends
		TestCase {

	public void testServerUserInfo() {
		ServerUserInfo serverUserInfo = new ServerUserInfo();
		serverUserInfo.setCasAuth(CasAuthType.AUTH_ADMIN);
		serverUserInfo.setDbCreateAuthType(DbCreateAuthType.AUTH_NONE);
		serverUserInfo.setPassword("1111");
		serverUserInfo.setStatusMonitorAuth(StatusMonitorAuthType.AUTH_MONITOR);
		serverUserInfo.setUserName("admin");
		List<DatabaseInfo> dbInfoList = new ArrayList<DatabaseInfo>();
		dbInfoList.add(new DatabaseInfo("pang", null));
		serverUserInfo.setDatabaseInfoList(dbInfoList);

		assertTrue(serverUserInfo.getCasAuth() == CasAuthType.AUTH_ADMIN);
		assertTrue(serverUserInfo.getDbCreateAuthType() == DbCreateAuthType.AUTH_NONE);
		assertTrue(serverUserInfo.getStatusMonitorAuth() == StatusMonitorAuthType.AUTH_MONITOR);
		assertEquals(serverUserInfo.getUserName(), "admin");
		assertEquals(serverUserInfo.getPassword(), "1111");
		assertTrue(serverUserInfo.getDatabaseInfoList().size() == 1
				&& serverUserInfo.getDatabaseInfoList().get(0).getDbName().equals(
						"pang"));
	}

}
