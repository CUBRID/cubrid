package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class CasAuthTypeTest extends
		TestCase {

	public void testCasAuthType() {
		CasAuthType type = CasAuthType.AUTH_ADMIN;
		assertEquals(type.getText(), "admin");
		type = CasAuthType.AUTH_MONITOR;
		assertEquals(type.getText(), "monitor");
		type = CasAuthType.AUTH_NONE;
		assertEquals(type.getText(), "none");
	}
}
