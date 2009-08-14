package com.cubrid.cubridmanager.core.cubrid.database.task;

import java.util.ArrayList;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;

public class CheckDbTaskTest extends
		SetupEnvTestCase {

	public void testExistDb() {

		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.CHECK_DATABASE_TASK_NAME,
				ServerManager.getInstance().getServer(host, monport),
				CommonSendMsg.commonDatabaseSendMsg);
		task.setDbName(dbname);
		


	}
	
	public void testNotExistDb() {

		CommonUpdateTask task = new CommonUpdateTask(
				CommonTaskName.CHECK_DATABASE_TASK_NAME,
				ServerManager.getInstance().getServer(host, monport),
				CommonSendMsg.commonDatabaseSendMsg);
		task.setDbName("demodb2");
		task.execute();
		task.fillSet(new ArrayList(),new String[] {"1","2"});
		task.isUsingSpecialDelimiter();
		task.getServerInfo();
		task.setServerInfo(site);
		task.isUsingMonPort();
		
		task.cancel();
		
		assertFalse(task.isSuccess());
		assertEquals(task.getErrorMsg(), "Database \"demodb2\" is unknown, or the file \"databases.txt\" cannot be accessed.");
		task.finish();
		task.isNeedServerConnected();
		task.setWarningMsg("error");
		task.isCancel();
	}
	
}
