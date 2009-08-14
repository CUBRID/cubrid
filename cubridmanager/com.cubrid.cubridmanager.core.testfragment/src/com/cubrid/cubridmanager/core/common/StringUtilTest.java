package com.cubrid.cubridmanager.core.common;

import junit.framework.TestCase;

public class StringUtilTest extends
		TestCase {

	public void testStringUtil() {
		String str = StringUtil.implode(",", new String[] { "4", "5" });
		assertEquals(str, "4,5");
		StringUtil.md5("what is my md5");
		assertTrue(StringUtil.isEmpty(""));
		assertTrue(StringUtil.isNotEmpty("nihao"));
		assertTrue(!StringUtil.isEqual(null, null));
		assertTrue(!StringUtil.isTrimEqual(null, null));
		assertEquals(StringUtil.nvl(null, "12345"), "12345");
	}
}
