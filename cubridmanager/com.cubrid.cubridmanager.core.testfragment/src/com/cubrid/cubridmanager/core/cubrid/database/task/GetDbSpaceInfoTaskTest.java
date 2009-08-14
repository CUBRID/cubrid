package com.cubrid.cubridmanager.core.cubrid.database.task;


import com.cubrid.cubridmanager.core.JsonObjectUtil;
import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;

public class GetDbSpaceInfoTaskTest extends SetupEnvTestCase {

	public void testSendMessage() {

		CommonQueryTask<DbSpaceInfoList> task = new CommonQueryTask<DbSpaceInfoList>(site,
		        CommonSendMsg.commonDatabaseSendMsg, new DbSpaceInfoList());
		task.setDbName(dbname);
		task.setUsingSpecialDelimiter(false);
		task.execute();
		
		DbSpaceInfoList bean=task.getResultModel();
		assertEquals(null, task.getErrorMsg());
		System.out.println(task.getErrorMsg());
		assertEquals(dbname, bean.getDbname());

		// test children
		assertEquals(true, bean.getSpaceinfo().size()>0);
		System.out.println("------------the result of task:dbspaceinfo in JSON-----------------");
		System.out.println(JsonObjectUtil.object2json(bean));
		System.out.println("-------------------------------------------------------------------");
	}


}
