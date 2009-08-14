package com.cubrid.cubridmanager.core.cubrid.jobauto.model;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;

public class SerialModelTest extends TestCase {
	public void testModelSerialInfo() {
		SerialInfo bean = new SerialInfo();
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.setOwner("owner");
		assertEquals(bean.getOwner(), "owner");
		bean.setCurrentValue("currentValue");
		assertEquals(bean.getCurrentValue(), "currentValue");
		bean.setIncrementValue("incrementValue");
		assertEquals(bean.getIncrementValue(), "incrementValue");
		bean.setMaxValue("maxValue");
		assertEquals(bean.getMaxValue(), "maxValue");
		bean.setMinValue("minValue");
		assertEquals(bean.getMinValue(), "minValue");
		bean.setStartedValue("startedValue");
		assertEquals(bean.getStartedValue(), "startedValue");
		bean.setClassName("className");
		assertEquals(bean.getClassName(), "className");
		bean.setAttName("attName");
		assertEquals(bean.getAttName(), "attName");

		bean.isCycle();
		bean.setCyclic(true);
		assertEquals(bean.isCycle(), true);
	}
}
