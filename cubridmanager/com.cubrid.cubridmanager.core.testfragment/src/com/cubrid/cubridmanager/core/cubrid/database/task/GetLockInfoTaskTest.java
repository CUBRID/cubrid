package com.cubrid.cubridmanager.core.cubrid.database.task;


import com.cubrid.cubridmanager.core.JsonObjectUtil;
import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.lock.DatabaseLockInfo;

public class GetLockInfoTaskTest extends SetupEnvTestCase {

	public void testSendMessage() {

		CommonQueryTask<DatabaseLockInfo> task = new CommonQueryTask<DatabaseLockInfo>(site,
		        CommonSendMsg.commonDatabaseSendMsg, new DatabaseLockInfo());
		task.setDbName("111ddd");
		task.setUsingSpecialDelimiter(false);
		task.execute();
		
		DatabaseLockInfo bean=task.getResultModel();
//		task.get
		assertEquals(null, task.getErrorMsg());
		System.out.println(task.getErrorMsg());
		

		// test children
		
		System.out.println("------------the result of task:dbspaceinfo in JSON-----------------");
		System.out.println(JsonObjectUtil.object2json(bean));
		System.out.println("-------------------------------------------------------------------");
	}


}
