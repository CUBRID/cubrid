package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;

public class DelBackupPlanTaskTest  extends SetupEnvTestCase{

	/*
	 * Test delete backupplan
	 *  @throws Exception
	 */
	public void testAddBackupPlanSend() throws Exception {
		DelBackupPlanTask task = new DelBackupPlanTask(site);
		task.setDbname("demodb");
		task.setBackupid("ccc");
        task.execute();
		task.setUsingSpecialDelimiter(false);
		//compare 
       assertTrue(task.isSuccess());
	}
	
	
}
