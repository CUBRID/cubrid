package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

public class SchemaInfoTest extends
		SetupJDBCTestCase {
	String testTableName = "testSchemaInfo";
	String createTestTableSQL = null;
	private String createSuperSQL1;
	private String createSuperSQL2;

	private void createTestTable() throws Exception {
		String filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.Schema/sup1.txt");
		String msg = Tool.getFileContent(filepath);
		createSuperSQL1 = msg;
		String[] strs = msg.split(";");
		boolean createSup1 = true;
		if (createSuperSQL1 != null) {
			for (String str : strs) {
				if (!str.trim().equals("")) {
					createSup1 = executeDDL(str);
				}
			}
		}
		assertTrue(createSup1);
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.Schema/sup2.txt");
		msg = Tool.getFileContent(filepath);
		createSuperSQL2 = msg;
		strs = msg.split(";");
		boolean createSup2 = true;
		if (createSuperSQL2 != null) {
			for (String str : strs) {
				if (!str.trim().equals("")) {
					createSup2 = executeDDL(str);
				}
			}
		}
		assertTrue(createSup2);
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.Schema/testTable.txt");
		msg = Tool.getFileContent(filepath);
		createTestTableSQL = msg;
		strs = msg.split(";");
		boolean createTestTable = true;
		if (createTestTableSQL != null) {
			for (String str : strs) {
				if (!str.trim().equals("")) {
					createTestTable = executeDDL(str);
				}
			}
		}
		assertTrue(createTestTable);
	}

	public void testGetDDL() throws Exception {
		createTestTable();
		/**
		 * init test, get schema information
		 */
		SchemaInfo schema = databaseInfo.getSchemaInfo(testTableName);

		List<String> superClasses = schema.getSuperClasses();
		List<SchemaInfo> supers = new ArrayList<SchemaInfo>();
		for (String sup : superClasses) {
			supers.add(databaseInfo.getSchemaInfo(sup));
		}

		/**
		 * test inherit attributes
		 */
		List<DBAttribute> inheritAttributes = schema.getInheritAttributes();
		assertEquals("smallint", inheritAttributes.get(0).getName());
		assertEquals(true, inheritAttributes.get(0).isNotNull());
		assertEquals(true, inheritAttributes.get(0).isUnique());
		assertEquals("sup1", inheritAttributes.get(0).getInherit());

		assertEquals("integer", inheritAttributes.get(1).getName());
		assertEquals("bigint", inheritAttributes.get(2).getName());
		assertEquals("numeric1", inheritAttributes.get(3).getName());
		assertEquals("numeric2", inheritAttributes.get(4).getName());
		assertEquals("float", inheritAttributes.get(5).getName());
		assertEquals("setint", inheritAttributes.get(6).getName());
		assertEquals("smallint2", inheritAttributes.get(7).getName());
		assertEquals("sup2", inheritAttributes.get(7).getInherit());
		assertEquals("cache", inheritAttributes.get(8).getName());
		assertEquals("sup2", inheritAttributes.get(8).getInherit());
		/**
		 * test inherit class attributes
		 */
		List<DBAttribute> inheritClassAttributes = schema.getInheritClassAttributes();
		assertEquals("float", inheritClassAttributes.get(0).getName());
		assertEquals("sup1", inheritClassAttributes.get(0).getInherit());
		/**
		 * test inherit PK
		 */
		List<Constraint> inheritPKs = schema.getInheritPK(supers);
		Constraint iPK = inheritPKs.get(0);
		String systemPKName = SystemNamingUtil.getPKName("sup1",
				iPK.getAttributes());
		assertEquals(systemPKName, iPK.getName());
		/**
		 * test inherit FK
		 */
		String fkName = "fk_sup2_smallint2";
		Constraint constraint = schema.getConstraintByName(fkName);
		Constraint fk = schema.getFKConstraint(fkName);
		assertTrue(constraint == fk);
		assertNotNull(fk);
		assertTrue(schema.isInSuperClasses(supers, fkName));
		/**
		 * test inherit \<Reverse\>Unique
		 */
		String uniqueName = "u_sup1_numeric1";
		Constraint unique = schema.getConstraintByName(uniqueName,
				ConstraintType.UNIQUE.getText());
		assertNotNull(unique);
		assertTrue(schema.isInSuperClasses(supers, uniqueName));
		/**
		 * test pk
		 */
		Constraint pk = schema.getPK(supers);
		assertNotNull(pk);
		String pkName = SystemNamingUtil.getPKName(schema.getClassname(),
				pk.getAttributes());
		assertEquals(pkName, pk.getName());

	}

	private void dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		executeDDL(sql);
		sql = "drop table sup2";
		executeDDL(sql);
		sql = "drop table sup1";
		executeDDL(sql);
	}

	@Override
	protected void tearDown() throws Exception {
		dropTestTable();
		super.tearDown();
	}
}