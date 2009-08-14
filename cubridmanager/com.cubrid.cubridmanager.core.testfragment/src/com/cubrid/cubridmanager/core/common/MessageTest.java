package com.cubrid.cubridmanager.core.common;

import junit.framework.TestCase;

public class MessageTest extends TestCase {

	public void testMessages() {
		
		assertEquals(com.cubrid.cubridmanager.core.Messages.error_unknownHost, "Unknown host.");
	}
}
