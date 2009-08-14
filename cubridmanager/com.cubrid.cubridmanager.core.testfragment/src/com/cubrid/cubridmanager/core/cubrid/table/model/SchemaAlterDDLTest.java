package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;
import com.cubrid.cubridmanager.core.Tool;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemeChangeLog.SchemeInnerType;

public class SchemaAlterDDLTest extends
		SetupJDBCTestCase {
	String testTableName = "testSchemaAlterDDLTest";
	String createTestTableSQL = null;
	private String createSuperSQL1;
	private String createSuperSQL2;
	private String result;

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
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.message/testAlterTable.txt");
		msg = Tool.getFileContent(filepath);
		createTestTableSQL = msg;
		strs = msg.split(";");
		if (createTestTableSQL != null) {
			for (String str : strs) {
				executeDDL(str);
			}
		}
		filepath = this.getFilePathInPlugin("com/cubrid/cubridmanager/core/cubrid/table/model/test.message/testAlterResult");
		msg = Tool.getFileContent(filepath);
		result = msg;

	}

	private boolean dropTestTable() {
		String sql = "drop table \"" + testTableName + "\"";
		executeDDL(sql);
		sql = "drop table sup2";
		executeDDL(sql);
		sql = "drop table sup1";
		return executeDDL(sql);
	}

	private String trimSQL(String sql) {
		if (sql == null || sql.indexOf("\n") == -1) {
			return sql;
		}
		String[] strs = sql.split("\n");
		StringBuffer bf = new StringBuffer();
		for (String str : strs) {
			String trim = str.trim();
			if (!trim.equals("")) {
				bf.append(trim).append("\n");
			}
		}
		return bf.toString();
	}

	public void testGetAlterDDL() throws Exception {
		createTestTable();
		SchemaInfo sup1 = databaseInfo.getSchemaInfo("sup1");
		SchemaInfo sup2 = databaseInfo.getSchemaInfo("sup2");
		SchemaInfo oldSchemaInfo = databaseInfo.getSchemaInfo(testTableName);
		SchemaChangeManager changeList = new SchemaChangeManager(databaseInfo,
				false);
		SchemaDDL ddl = new SchemaDDL(changeList, databaseInfo);
		SchemaInfo newSchema = oldSchemaInfo.clone();

		List<String> newSuperclass = new ArrayList<String>();
		newSuperclass.add("sup1");
		newSuperclass.add("sup2");
		boolean success = SuperClassUtil.fireSuperClassChanged(databaseInfo,
				oldSchemaInfo, newSchema, newSuperclass);
		newSchema.setSuperClasses(newSuperclass);
		String alterDDL = ddl.getAlterDDL(oldSchemaInfo, newSchema);
		assertEquals(trimSQL(result), trimSQL(alterDDL));

		String type = "";
		String superTable = "sup1";
		String column = "bigint";
		List<DBResolution> resolutions = null;
		DBResolution removedResolution = null;
		if (type.equals("Class")) { //$NON-NLS-1$
			resolutions = newSchema.getClassResolutions();
		} else {
			resolutions = newSchema.getResolutions();
		}
		for (int i = 0; i < resolutions.size(); i++) {
			DBResolution r = resolutions.get(i);
			if (r.getName().equals(column)
					&& r.getClassName().equals(superTable)) {
				removedResolution = resolutions.remove(i);
			}
		}
		if (removedResolution.getAlias().equals("")) {
			List<String[]> columnConflicts = null;
			boolean isClassType;
			if (type.equals("Class")) { //$NON-NLS-1$
				isClassType = true;
			} else {
				isClassType = false;
			}
			columnConflicts = SuperClassUtil.getColumnConflicts(databaseInfo,
					newSchema, newSchema.getSuperClasses(), isClassType);
			DBResolution nextResolution = SuperClassUtil.getNextResolution(
					resolutions, removedResolution, columnConflicts);
			assert (nextResolution != null);
			newSchema.addResolution(nextResolution, isClassType);

			SuperClassUtil.fireResolutionChanged(databaseInfo, oldSchemaInfo,
					newSchema, isClassType);
		}

		type = "CLASS";
		superTable = "sup1";
		column = "float";

		if (type.equals("Class")) { //$NON-NLS-1$
			resolutions = newSchema.getClassResolutions();
		} else {
			resolutions = newSchema.getResolutions();
		}
		for (int i = 0; i < resolutions.size(); i++) {
			DBResolution r = resolutions.get(i);
			if (r.getName().equals(column)
					&& r.getClassName().equals(superTable)) {
				removedResolution = resolutions.remove(i);
			}
		}
		if (removedResolution.getAlias().equals("")) {
			List<String[]> columnConflicts = null;
			boolean isClassType;
			if (type.equals("Class")) { //$NON-NLS-1$
				isClassType = true;
			} else {
				isClassType = false;
			}
			columnConflicts = SuperClassUtil.getColumnConflicts(databaseInfo,
					newSchema, newSchema.getSuperClasses(), isClassType);
			DBResolution nextResolution = SuperClassUtil.getNextResolution(
					resolutions, removedResolution, columnConflicts);
			assert (nextResolution != null);
			newSchema.addResolution(nextResolution, isClassType);

			SuperClassUtil.fireResolutionChanged(databaseInfo, oldSchemaInfo,
					newSchema, isClassType);
		}
		String newAttrName = "newAttr";
		DBAttribute attr = new DBAttribute();
		attr.setName(newAttrName);
		attr.setType("bigint");
		attr.setDefault("12");
		attr.setInherit(newSchema.getClassname());
		newSchema.addAttribute(attr);
		changeList.addSchemeChangeLog(new SchemeChangeLog(null, newAttrName,
				SchemeInnerType.TYPE_ATTRIBUTE));

		changeList.addSchemeChangeLog(new SchemeChangeLog("varchar", null,
				SchemeInnerType.TYPE_ATTRIBUTE));
		alterDDL = ddl.getAlterDDL(oldSchemaInfo, newSchema);

		dropTestTable();

	}

}
