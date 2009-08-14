package com.cubrid.cubridmanager.core.cubrid.database.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;

public class GetDbSizeInfoTaskTest extends SetupEnvTestCase {

	public void testSendMessage() {

		GetDbSizeTask task = new GetDbSizeTask(site);

		task.setUsingSpecialDelimiter(false);
		task.setDbName(dbname);
		task.execute();
		int dbSize = task.getDbSize();
		assertEquals(null, task.getErrorMsg());
		System.out.println(task.getErrorMsg());
		assertEquals(true, dbSize > 0);
		System.out.println("the db "+dbname+"'s size:"+dbSize);
	}

}
