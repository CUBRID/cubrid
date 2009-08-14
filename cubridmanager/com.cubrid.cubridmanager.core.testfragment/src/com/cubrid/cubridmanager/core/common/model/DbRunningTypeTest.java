package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class DbRunningTypeTest extends
		TestCase {

	public void testDbRunningType() {
		DbRunningType type = DbRunningType.CS;
		assertTrue(type == DbRunningType.CS);
		type = DbRunningType.STANDALONE;
		assertTrue(type == DbRunningType.STANDALONE);
		type = DbRunningType.NONE;
		assertTrue(type == DbRunningType.NONE);
	}

}
