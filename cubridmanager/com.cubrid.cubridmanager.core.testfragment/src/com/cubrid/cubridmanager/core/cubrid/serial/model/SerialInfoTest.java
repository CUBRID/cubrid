package com.cubrid.cubridmanager.core.cubrid.serial.model;

import junit.framework.TestCase;

public class SerialInfoTest extends
		TestCase {

	public void testSerialInfo() {
		SerialInfo serialInfo = new SerialInfo();
		serialInfo.setAttName("id");
		serialInfo.setClassName("code");
		serialInfo.setCurrentValue("1000");
		serialInfo.setCyclic(true);
		serialInfo.setIncrementValue("1");
		serialInfo.setMaxValue("10000");
		serialInfo.setMinValue("1");
		serialInfo.setName("serial1");
		serialInfo.setOwner("dba");
		serialInfo.setStartedValue("1");
		assertEquals(serialInfo.getAttName(), "id");
		assertEquals(serialInfo.getClassName(), "code");
		assertEquals(serialInfo.getCurrentValue(), "1000");
		assertTrue(serialInfo.isCycle());
		assertEquals(serialInfo.getIncrementValue(), "1");
		assertEquals(serialInfo.getMaxValue(), "10000");
		assertEquals(serialInfo.getMinValue(), "1");
		assertEquals(serialInfo.getName(), "serial1");
		assertEquals(serialInfo.getOwner(), "dba");
		assertEquals(serialInfo.getStartedValue(), "1");
		SerialInfo clonedSerialInfo = serialInfo.clone();
		assertEquals(serialInfo, clonedSerialInfo);
		serialInfo.hashCode();
	}

}
