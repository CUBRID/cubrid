package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;

public class GetPartitionedClassListTaskTest extends
		SetupJDBCTestCase {
	String testTableName = "testGetPartitionedClassListTaskTest";
	String sql = null;

	private boolean createTestTable() {
		String sql = "create table \"" + testTableName + "\" ("
				+ "empno char(10) not null unique,"
				+ "empname varchar(20) not null," + "deptname varchar(20), "
				+ "hiredate date" + ")"
				+ "partition by range (extract (year from hiredate)) "
				+ "(partition h2000 values less than (2000),"
				+ "partition h2003 values less than (2003),"
				+ "partition hmax values less than  maxvalue)";
		;
		return executeDDL(sql);
	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		return executeDDL(sql);
	}

	public void testGetPartitionedClassListTaskTest() {
		createTestTable();
		GetPartitionedClassListTask task = new GetPartitionedClassListTask(
				databaseInfo, site, null, port);
		List<ClassInfo> list=task.getAllPartitionedClassInfoList(testTableName);		
		assertEquals(3, list.size());
		dropTestTable();
	}
}