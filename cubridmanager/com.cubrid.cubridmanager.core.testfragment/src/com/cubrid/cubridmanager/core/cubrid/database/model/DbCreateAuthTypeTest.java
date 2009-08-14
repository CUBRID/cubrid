package com.cubrid.cubridmanager.core.cubrid.database.model;

import junit.framework.TestCase;

public class DbCreateAuthTypeTest extends
		TestCase {

	public void testDbCreateAuthType() {
		DbCreateAuthType type = DbCreateAuthType.AUTH_ADMIN;
		assertEquals(type.getText(), "admin");
		type = DbCreateAuthType.AUTH_NONE;
		assertEquals(type.getText(), "none");
	}

}
