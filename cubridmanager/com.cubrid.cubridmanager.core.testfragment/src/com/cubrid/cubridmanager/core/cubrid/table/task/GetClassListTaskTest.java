package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;

public class GetClassListTaskTest extends
		SetupEnvTestCase {

	public void testGetClassListTaskTest() {

		GetClassListTask task = new GetClassListTask(site);
		task.setDbName(dbname);
		task.setDbStatus(OnOffType.OFF);
		task.execute();
		assertEquals(true, task.isSuccess());
		DBClasses db=task.getDbClassInfo();
		assertEquals(dbname, db.getDbname());
		

	}
}