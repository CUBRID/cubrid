package com.cubrid.cubridmanager.core.cubrid.table.task;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;

public class GetSchemaTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetRecordCountTaskTest";
	String sql = null;

	private boolean createTestTable() {
		String sql = "create table \"" + testTableName + "\" ("
				+ "code integer AUTO_INCREMENT," + "name character varying(40)  NOT NULL,"
				+ "gender character(1) ," + "nation_code set_of(integer) " + ")";
		return executeDDL(sql);
	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		return executeDDL(sql);
	}

	public void testGetSchemaTask() {
		createTestTable();
		GetSchemaTask task = new GetSchemaTask(databaseInfo, testTableName);

		task.execute();
		SchemaInfo jSchema = task.getSchema();

		ClassTask task2 = new ClassTask(site);
		task2.setDbName(dbname);
		task2.setClassName(testTableName);
		task2.execute();
		SchemaInfo tSchema = task2.getSchemaInfo();
		System.out.println(jSchema);
		System.out.println(tSchema);
		dropTestTable();

	}

}
