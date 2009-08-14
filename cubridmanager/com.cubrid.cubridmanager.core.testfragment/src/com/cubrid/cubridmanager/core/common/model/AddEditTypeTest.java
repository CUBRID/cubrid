package com.cubrid.cubridmanager.core.common.model;

import junit.framework.TestCase;

public class AddEditTypeTest extends
		TestCase {

	public void testAddEditType() {
		AddEditType type = AddEditType.ADD;
		assertEquals(type.getText(), "add");
		type = AddEditType.EDIT;
		assertEquals(type.getText(), "edit");
	}

}
