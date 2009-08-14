package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.Tool;

public class SchemaDDLTest extends
		SetupJDBCTestCase {
	String testTableName = "testSchemaDDLTest";
	String createTestTableSQL = null;
	private String createSuperSQL1;
	private String createSuperSQL2;

	private void createTestTable() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.message/sup1.txt");
		String msg = Tool.getFileContent(filepath);
		createSuperSQL1 = msg;
		String[] strs = msg.split(";");
		if (createSuperSQL1 != null) {
			for (String str : strs) {
				executeDDL(str);
			}
		}
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.message/sup2.txt");
		msg = Tool.getFileContent(filepath);
		createSuperSQL2 = msg;
		strs = msg.split(";");
		if (createSuperSQL2 != null) {
			for (String str : strs) {
				executeDDL(str);
			}
		}
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.message/testTable.txt");
		msg = Tool.getFileContent(filepath);
		createTestTableSQL = msg;
		strs = msg.split(";");
		if (createTestTableSQL != null) {
			for (String str : strs) {
				executeDDL(str);
			}
		}
	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		executeDDL(sql);
		sql = "drop table sup1";
		executeDDL(sql);
		sql = "drop table sup2";
		return executeDDL(sql);
	}

	private String trimSQL(String sql) {
		if (sql == null||sql.indexOf("\n")==-1) {
			return sql;
		}
		String[] strs = sql.split("\n");
		StringBuffer bf=new StringBuffer();
		for(String str:strs){
			String trim = str.trim();
			if(!trim.equals("")){
				bf.append(trim).append("\n");
			}			
		}
		return bf.toString();
	}

	public void testGetDDL() throws Exception {
		createTestTable();
		SchemaInfo sup1 = databaseInfo.getSchemaInfo("sup1");
		SchemaInfo sup2 = databaseInfo.getSchemaInfo("sup2");
		SchemaInfo schema = databaseInfo.getSchemaInfo(testTableName);
		SchemaChangeManager changeList = new SchemaChangeManager(databaseInfo,
				false);
		SchemaDDL ddl = new SchemaDDL(changeList, databaseInfo);
		String retSQL = ddl.getDDL(schema);
		assertEquals(trimSQL(createTestTableSQL), trimSQL(retSQL));
		retSQL = ddl.getDDL(sup1);
		trimSQL(createSuperSQL1);
		trimSQL(retSQL);
		retSQL = ddl.getDDL(sup2);
		assertEquals(trimSQL(createSuperSQL2), trimSQL(retSQL));
		SchemaInfo newSchema = schema.clone();
		assertTrue(newSchema.equals(schema));
		newSchema.toString();
		dropTestTable();

	}

	public void testSuperClassChanged() {
		SchemaDDL ddl = new SchemaDDL(null, null);
		List<List<String>> result = null;
		List<String> oldSupers = new ArrayList<String>();
		List<String> newSupers = new ArrayList<String>();
		oldSupers.add("1");
		oldSupers.add("2");
		oldSupers.add("3");

		newSupers.add("2"); //expect remove "1"
		newSupers.add("4"); //expect remove "3"
		newSupers.add("1");

		result = ddl.getSuperclassChanges(oldSupers, newSupers);
		assertEquals(result.size(), 2);
		assertEquals(result.get(0).toString(), "[1, 3]");
		assertEquals(result.get(1).toString(), "[4, 1]");

		oldSupers.clear();
		newSupers.clear();

		newSupers.add("2");
		newSupers.add("4");
		newSupers.add("1");

		result = ddl.getSuperclassChanges(oldSupers, newSupers);
		assertEquals(result.size(), 1);
		assertEquals(result.get(0).toString(), "[2, 4, 1]");

		oldSupers.clear();
		newSupers.clear();
		oldSupers.add("1");
		oldSupers.add("2");
		oldSupers.add("3");
		oldSupers.add("4");
		oldSupers.add("5");

		newSupers.add("1");
		newSupers.add("2");
		newSupers.add("5");//expect remove "3,4"

		result = ddl.getSuperclassChanges(oldSupers, newSupers);
		assertEquals(result.size(), 1);
		assertEquals(result.get(0).toString(), "[4, 3]");

	}

}
