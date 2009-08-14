package com.cubrid.cubridmanager.core.cubrid.sp;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;

public class CommonSqlExcuteTaskTest extends SetupJDBCTestCase {

	public void testCommonSqlExcuteTask() {
		CommonSQLExcuterTask task1 = new CommonSQLExcuterTask(
				databaseInfo, site,databaseInfo.getDriverPath(), databaseInfo.getBrokerPort());
		GetAllClassListTask classTask = new GetAllClassListTask(
				databaseInfo, site,databaseInfo.getDriverPath(), databaseInfo.getBrokerPort());
		classTask.setTableName("assdfafa");
		classTask.getClassInfoTaskExcute();
		ClassInfo classInfo=classTask.getClassInfo();
		if(classInfo==null){
			task1.addSqls("create table assdfafa");
			task1.addCallSqls("call change_owner ('assdfafa','public') on class db_authorizations");
			assertEquals(1, task1.getSqls().size());
		}else{
			task1.addCallSqls("call change_owner ('assdfafa','public') on class db_authorizations");
		}
		assertEquals(1, task1.getCallSqls().size());
		
		task1.execute();
		System.out.println(task1.getErrorMsg());
		task1.setErrorCode(-1);
		task1.getCurrentDDL();
		System.out.println(task1.getErrorCode());
		assertEquals(null, task1.getErrorMsg());
		
//		task.addSqls("create table assdfafa");
//		task.addCallSqls("call change_owner ('DB_ROOT','DBA') on class db_authorizations");
		
		
		task1 = new CommonSQLExcuterTask(
				databaseInfo, site,databaseInfo.getDriverPath(), databaseInfo.getBrokerPort());
		task1.addSqls("drop table assdfafa");
		task1.execute();
		System.out.println(task1.getErrorMsg());
		assertEquals(null, task1.getErrorMsg());

	}

}
