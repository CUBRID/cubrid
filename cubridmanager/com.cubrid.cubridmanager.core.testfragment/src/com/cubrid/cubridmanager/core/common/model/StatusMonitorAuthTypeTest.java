package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class StatusMonitorAuthTypeTest extends
		TestCase {

	public void testStatusMonitorAuthType() {
		StatusMonitorAuthType type = StatusMonitorAuthType.AUTH_ADMIN;
		assertEquals(type.getText(), "admin");
		type = StatusMonitorAuthType.AUTH_MONITOR;
		assertEquals(type.getText(), "monitor");
		type = StatusMonitorAuthType.AUTH_NONE;
		assertEquals(type.getText(), "none");
	}
}
