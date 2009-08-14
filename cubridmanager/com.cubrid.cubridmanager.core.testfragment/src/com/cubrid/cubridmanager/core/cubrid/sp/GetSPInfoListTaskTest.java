package com.cubrid.cubridmanager.core.cubrid.sp;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPType;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.core.cubrid.sp.task.GetSPInfoListTask;

public class GetSPInfoListTaskTest extends SetupJDBCTestCase {

	public void testCommonSqlExcuteTask() {

		CommonSQLExcuterTask task1 = new CommonSQLExcuterTask(databaseInfo, site, databaseInfo.getDriverPath(),
		        databaseInfo.getBrokerPort());
		task1
		        .addSqls("CREATE FUNCTION \"sssssss\"(\"sss\" CHAR) RETURN CHAR AS LANGUAGE JAVA NAME '111.d(java.sql.Timestamp) return java.lang.String'");
		task1.execute();
		assertEquals(null, task1.getErrorMsg());
		final GetSPInfoListTask task = new GetSPInfoListTask(databaseInfo, site, databaseInfo.getDriverPath(),
		        databaseInfo.getBrokerPort());
		task.setSpName("sssssss");
		task.setSpType(SPType.FUNCTION);
		task.execute();
		assertTrue(task.getSPInfoList().size() == 1);
		assertEquals(false, task.isCancel());

		task1 = new CommonSQLExcuterTask(databaseInfo, site, databaseInfo.getDriverPath(), databaseInfo.getBrokerPort());
		task1.addSqls("drop FUNCTION \"sssssss\"");
		task1.execute();
		assertEquals(null, task1.getErrorMsg());
	}

}
