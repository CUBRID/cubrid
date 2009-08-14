package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class EnvInfoTest extends
		TestCase {

	public void testEnvInfo() {
		EnvInfo envInfo = new EnvInfo();
		envInfo.setBrokerVersion("8.2.0");
		envInfo.setCmServerDir("/home/daniel/cm");
		envInfo.setDatabaseDir("/home/daniel/demodb");
		envInfo.setHostMonTabStatus(new String[] { "status1", "status2" });
		envInfo.setOsInfo("linux");
		envInfo.setRootDir("/home/daniel");
		envInfo.setServerVersion("8.2.0.1023");

		assertEquals(envInfo.getBrokerVersion(), "8.2.0");
		assertEquals(envInfo.getCmServerDir(), "/home/daniel/cm");
		assertEquals(envInfo.getDatabaseDir(), "/home/daniel/demodb");
		assertEquals(envInfo.getOsInfo(), "linux");
		assertEquals(envInfo.getRootDir(), "/home/daniel");
		assertEquals(envInfo.getServerVersion(), "8.2.0.1023");
		assertTrue(envInfo.getHostMonTabStatus().length == 2);
		assertEquals(envInfo.getHostMonTabStatus()[0], "status1");
		assertEquals(envInfo.getHostMonTabStatus()[1], "status2");
	}
}
