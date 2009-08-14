package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class OnOffTypeTest extends
		TestCase {

	public void testOnOffType() {
		OnOffType type = OnOffType.ON;
		assertEquals(type.getText(), "ON");
		type = OnOffType.OFF;
		assertEquals(type.getText(), "OFF");
	}
}
