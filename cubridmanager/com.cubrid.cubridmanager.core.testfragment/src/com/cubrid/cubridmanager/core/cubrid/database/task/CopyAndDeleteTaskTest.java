package com.cubrid.cubridmanager.core.cubrid.database.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.YesNoType;

public class CopyAndDeleteTaskTest extends SetupEnvTestCase {
	String newdbname = "aaaaaaaaaaa";

	public void testCopyDataBase() {

		// task:copydb
		// token:8ec1ab8a91333c78403a39727783958b5f3f25c536c9d92f88b7d31a95a5fadf7926f07dd201b6aa
		// srcdbname:aaa
		// destdbname:dss
		// destdbpath:C:\CUBRID\databases\dss
		// exvolpath:C:\CUBRID\databases\dss
		// logpath:C:\CUBRID\databases\
		// overwrite:n
		// move:n
		// advanced:on
		// open:volume
		// C|/CUBRID/databases/aaa/aaa:C|\CUBRID\databases\dss/dss
		// C|/CUBRID/databases/aaa/aaa_x001:C|\CUBRID\databases\dss/dss_x001
		// C|/CUBRID/databases/aaa/aaa_x002:C|\CUBRID\databases\dss/dss_x002
		// close:volume
		
		final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(
				site);
		getDatabaseListTask.execute();
		 List<DatabaseInfo> databaseInfoList=getDatabaseListTask.loadDatabaseInfo();
		
		DatabaseInfo databaseInfo = null;
		for (DatabaseInfo bean : databaseInfoList) {
			if (bean.getRunningType() == DbRunningType.STANDALONE) {
				databaseInfo = bean;
				break;
			}
		}
		boolean stopDb = false;
		if (databaseInfo == null) {
			stopDb = true;
			databaseInfo = databaseInfoList.get(0);
			CommonUpdateTask stopTask = new CommonUpdateTask(CommonTaskName.STOP_DB_TASK_NAME, site,
			        CommonSendMsg.commonDatabaseSendMsg);
			stopTask.setDbName(databaseInfo.getDbName());
			//stopTask.execute();
			assertEquals(null, stopTask.getErrorMsg());
		}
		// check dir
		CheckDirTask checkDirTask = new CheckDirTask(site);
		checkDirTask.setDirectory(new String[]
			{
				serverPath + getPathSeparator() + "databases" + getPathSeparator() + newdbname
			});
		//checkDirTask.execute();

		assertEquals(null, checkDirTask.getErrorMsg());
/*		if (checkDirTask.getNoExistDirectory() != null && checkDirTask.getNoExistDirectory().length > 0) {

		}*/
		CopyDbTask task = new CopyDbTask(site);
		task.setSrcdbname(databaseInfo.getDbName());

		task.setDestdbname(newdbname);
		task.setDestdbpath(serverPath + getPathSeparator() + "databases" + getPathSeparator() + newdbname);
		task.setExvolpath(serverPath + getPathSeparator() + "databases" + getPathSeparator() + newdbname);
		task.setLogpath(serverPath + getPathSeparator() + "databases" + getPathSeparator() + newdbname);
		task.setOverwrite(YesNoType.N);
		task.setMove(YesNoType.N);
		task.setAdvanced(OnOffType.OFF);

		//task.execute();
		System.out.println("getErrorMsg:" + task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

		if (stopDb) {
			CommonUpdateTask startTask = new CommonUpdateTask(CommonTaskName.START_DB_TASK_NAME, site,
			        CommonSendMsg.commonDatabaseSendMsg);
			startTask.setDbName(databaseInfo.getDbName());
			//startTask.execute();
			assertEquals(null, startTask.getErrorMsg());
		}
	}

	public void testDeleteDataBase() {

		CommonUpdateTask task = new CommonUpdateTask(CommonTaskName.DELETE_DATABASE_TASK_NAME, site,
		        CommonSendMsg.deletedbSendMsg);

		// task:deletedb
		// token:8ec1ab8a91333c787d7a795ca45264bae103ddfeface7072c47a07a0b1f66f6f7926f07dd201b6aa
		// dbname:demodb
		// delbackup:y

		task.setDbName(newdbname);
		task.setDelbackup(YesNoType.Y);
		//task.execute();
		System.out.println(task.getErrorMsg());
		assertEquals(null, task.getErrorMsg());

	}

}
