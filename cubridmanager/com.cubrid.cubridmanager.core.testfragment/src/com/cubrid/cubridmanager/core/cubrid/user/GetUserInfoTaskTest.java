package com.cubrid.cubridmanager.core.cubrid.user;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.UserSendObj;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetDatabaseListTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.core.cubrid.user.task.UpdateAddUserTask;

public class GetUserInfoTaskTest extends SetupEnvTestCase {

	public void testUpdateMessage() {
		
		final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(
				site);
		getDatabaseListTask.execute();
		 List<DatabaseInfo> databaseInfoList=getDatabaseListTask.loadDatabaseInfo();
		
		DatabaseInfo databaseInfo = null;
		for (DatabaseInfo db : databaseInfoList) {
			if (dbname.equals(db.getDbName()))
				databaseInfo = db;
		}
		if (databaseInfo == null)
			for (DatabaseInfo db : databaseInfoList) {
				if (db.getRunningType() != DbRunningType.STANDALONE)
					databaseInfo = db;
			}
		// test get user list

		final CommonQueryTask<DbUserInfoList> userTask = new CommonQueryTask<DbUserInfoList>(site,
		        CommonSendMsg.commonDatabaseSendMsg, new DbUserInfoList());
		userTask.setDbName(databaseInfo.getDbName());
		userTask.execute();
		assertEquals(null, userTask.getErrorMsg());
		assertEquals(true, userTask.getResultModel()!=null);
		
		List<DbUserInfo> userListInfo = userTask.getResultModel().getUserList();
		
		String newUserName=getUserName(userListInfo,"a");

		// test add user 
		UpdateAddUserTask task = new UpdateAddUserTask(site, true);
		UserSendObj userSendObj = new UserSendObj();
		userSendObj.setDbname(databaseInfo.getDbName());

		userSendObj.setUsername(newUserName);
		userSendObj.setUserpass("1");
		userSendObj.addGroups("public");
		task.setUserSendObj(userSendObj);
		task.execute();
		task.getUserSendObj();
		task.isSuccess();
		task.setUserName("dba");
		task.setDbName(dbname);
		
		System.out.println("--------------------------- add user msg:" + task.getErrorMsg()
		        + "----------------------------------------");
		assertEquals(null, task.getErrorMsg());
		
		// test edit user 
		task = new UpdateAddUserTask(site, false);
		userSendObj = new UserSendObj();
		userSendObj.setDbname(databaseInfo.getDbName());

		userSendObj.setUsername(newUserName);
		userSendObj.setUserpass("2");
		userSendObj.addGroups("public");
		task.setUserSendObj(userSendObj);
		task.execute();
		
		System.out.println("--------------------------- eidt user msg:" + task.getErrorMsg()
		        + "----------------------------------------");
		assertEquals(null, task.getErrorMsg());
		// test delete user
		CommonUpdateTask commonTask = new CommonUpdateTask(
				CommonTaskName.DELETE_USER_TASK_NAME,
				site,
				CommonSendMsg.deleteUserMSGItems);
		commonTask.setDbName(databaseInfo.getDbName());

		commonTask.setUserName(newUserName);

		commonTask.execute();
		assertEquals(null, commonTask.getErrorMsg());
		System.out.println("---------------------------msg:" + task.getErrorMsg()
		        + "----------------------------------------");
	}

	public String getUserName(List<DbUserInfo> userListInfo,String userName){
		for(DbUserInfo u:userListInfo){
			if(u.getName().equalsIgnoreCase(userName)){
				return getUserName(userListInfo,userName+"a");
			}
		} 
		return userName;
	}
}
