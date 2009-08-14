package com.cubrid.cubridmanager.core.cubrid.database.model;

import junit.framework.TestCase;

public class DbUnloadInfoTest extends
		TestCase {

	public void testDbUnloadInfo() {
		DbUnloadInfo dbUnloadInfo = new DbUnloadInfo();
		dbUnloadInfo.setDbName("demodb");
		dbUnloadInfo.getIndexDateList().add("2009/09/08");
		dbUnloadInfo.getIndexPathList().add("/home/daniel/index");
		dbUnloadInfo.getObjectDateList().add("2009/09/07");
		dbUnloadInfo.getObjectPathList().add("/home/daniel/object");
		dbUnloadInfo.getSchemaDateList().add("2009/09/06");
		dbUnloadInfo.getSchemaPathList().add("/home/daniel/schema");
		dbUnloadInfo.getTriggerDateList().add("2009/09/05");
		dbUnloadInfo.getTriggerPathList().add("/home/daniel/trigger");
		assertEquals(dbUnloadInfo.getDbName(), "demodb");
		assertEquals(dbUnloadInfo.getIndexDateList().get(0), "2009/09/08");
		assertEquals(dbUnloadInfo.getIndexPathList().get(0),
				"/home/daniel/index");

		assertEquals(dbUnloadInfo.getObjectDateList().get(0), "2009/09/07");
		assertEquals(dbUnloadInfo.getObjectPathList().get(0),
				"/home/daniel/object");

		assertEquals(dbUnloadInfo.getSchemaDateList().get(0), "2009/09/06");
		assertEquals(dbUnloadInfo.getSchemaPathList().get(0),
				"/home/daniel/schema");

		assertEquals(dbUnloadInfo.getTriggerDateList().get(0), "2009/09/05");
		assertEquals(dbUnloadInfo.getTriggerPathList().get(0),
				"/home/daniel/trigger");

	}
}
