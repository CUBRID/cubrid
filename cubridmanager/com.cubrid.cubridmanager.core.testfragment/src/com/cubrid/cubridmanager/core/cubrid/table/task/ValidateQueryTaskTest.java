package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class ValidateQueryTaskTest extends SetupJDBCTestCase {

	public void testExecute() throws Exception {
		ValidateQueryTask task = new ValidateQueryTask(databaseInfo, site, databaseInfo.getDriverPath(), databaseInfo
		        .getBrokerPort());
		task.addSqls("select * from db_user");
		assertEquals(task.getSqls().size(),1);
		task.setErrorCode(-1);
		assertEquals(task.getErrorCode(),-1);
		task.execute();
		assertEquals(task.getResult().size()>0,true);
	}
}
