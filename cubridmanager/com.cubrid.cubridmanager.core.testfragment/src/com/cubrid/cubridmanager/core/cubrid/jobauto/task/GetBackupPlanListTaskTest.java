package com.cubrid.cubridmanager.core.cubrid.jobauto.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;

/**
 * Tests Type GetBackupPlanListTaskTest
 * 
 * @author lizhiqiang Apr 3, 2009
 */
public class GetBackupPlanListTaskTest extends
		SetupEnvTestCase {
	/**
	 * Tests getBackupPlanListTask method
	 * 
	 */
	public void testReceive() {
		GetBackupPlanListTask getBackupPlanListTask = new GetBackupPlanListTask(
				site);
		getBackupPlanListTask.setDbName("demodb");
		getBackupPlanListTask.execute();
		List<BackupPlanInfo> list=getBackupPlanListTask.getBackupPlanInfoList();
	}

}
