package com.cubrid.cubridmanager.core;

import java.lang.reflect.InvocationTargetException;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.cubrid.database.model.UserSendObj;

public class CommonToolTest extends TestCase {

	public void testStr2Int() {
		String str1 = "1234";
		int val1 = CommonTool.str2Int(str1);
		assertEquals(val1, 1234);
		String str2 = "str2";
		int val2 = CommonTool.str2Int(str2);
		assertEquals(val2, 0);
	}

	public void testStr2Double() {
		String str1 = "123.123";
		double val1 = CommonTool.str2Double(str1);
		assertEquals(val1, 123.123);
		String str2 = "str2";
		double val2 = CommonTool.str2Double(str2);
		assertEquals(val2, 0.0);
	}

	public void testCopyBean2Bean() {
		UserSendObj u1 = new UserSendObj();
		UserSendObj u2 = new UserSendObj();
		u1.setDbname("dbname");
		try {
			CommonTool.copyBean2Bean(u1, u2);
		} catch (IllegalAccessException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		} catch (InvocationTargetException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		assertEquals(u2.getDbname(), "dbname");
	}

	
}
