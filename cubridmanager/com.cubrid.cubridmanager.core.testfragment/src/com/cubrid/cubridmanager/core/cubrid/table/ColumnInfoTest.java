package com.cubrid.cubridmanager.core.cubrid.table;

import junit.framework.TestCase;

public class ColumnInfoTest extends
		TestCase {
	public void testColumnInfo(){
		ColumnInfo col=new ColumnInfo("id","Char(1)");
		assertEquals("id", col.getName());
		assertEquals("Char(1)", col.getType());
		col.setName("id2");
		col.setType("Char(2)");
		assertEquals("id2", col.getName());
		assertEquals("Char(2)", col.getType());
		
	}

}
