package com.cubrid.cubridmanager.core.cubrid.trigger.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class GetTriggerListByUserNameTaskTest extends
		SetupJDBCTestCase {

	public void testTriggerTask() {
		//test create serial
		GetTriggerListByUserNameTask getTriggerListByUserNameTask = new GetTriggerListByUserNameTask(
				databaseInfo, site, null, brokerPort);
		List list = getTriggerListByUserNameTask.getTriggerList("woshishui");
		assertTrue(list == null || list.size() == 0);
	}
}
