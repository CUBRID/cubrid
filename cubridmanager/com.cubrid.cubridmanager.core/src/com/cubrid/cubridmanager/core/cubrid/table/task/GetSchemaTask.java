/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;

/**
 * get schema information via JDBC
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-16 created by moulinwang
 */
public class GetSchemaTask extends
		JDBCTask {

	String tableName;
	private SchemaInfo schema = null;

	public GetSchemaTask(DatabaseInfo dbInfo, String tableName) {
		super("get schema information", dbInfo,  false);
		this.tableName = tableName.toLowerCase();
	}

	public void execute() {
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			//get table information
			String sql = "select class_type,is_system_class,owner_name "
					+ "from db_class where class_name =?";
			stmt = connection.prepareStatement(sql);
			((PreparedStatement) stmt).setString(1, tableName);
			rs = ((PreparedStatement) stmt).executeQuery();
			SchemaInfo ret = null;
			while (rs.next()) {
				String type = rs.getString("class_type");
				String isSystemClass = rs.getString("is_system_class");
				String owner = rs.getString("owner_name");
				ret = new SchemaInfo();
				if (type.equals("CLASS")) {
					ret.setVirtual("normal");
				} else {
					ret.setVirtual("view");
				}
				if (isSystemClass.equals("NO")) {
					ret.setType("user");
				} else {
					ret.setVirtual("system");
				}
				ret.setOwner(owner);

				ret.setClassname(tableName);
				ret.setDbname(databaseInfo.getDbName());
			}
			try {
				rs.close();
				stmt.close();
			} catch (SQLException ignore) {
			}
			// get super class information
			if (ret != null) {
				sql = "select super_class_name from db_direct_super_class"
						+ " where class_name =?";
				stmt = connection.prepareStatement(sql);
				((PreparedStatement) stmt).setString(1, tableName);
				rs = ((PreparedStatement) stmt).executeQuery();
				while (rs.next()) {
					String superClass = rs.getString(1);
					ret.addSuperClass(superClass);
				}
				try {
					rs.close();
					stmt.close();
				} catch (SQLException ignore) {
				}
			}
			// get column information
			if (ret != null) {
				sql = "SELECT a.attr_name, a.attr_type, a.from_class_name,"
						+ " a.data_type, a.prec, a.scale, a.is_nullable, "
						+ " a.code_set, a.domain_class_name, a.default_value, a.def_order"
						+ " FROM db_attribute a" + " WHERE a.class_name=? "
						+ " order by a.def_order";
				stmt = connection.prepareStatement(sql);
				((PreparedStatement) stmt).setString(1, tableName);
				rs = ((PreparedStatement) stmt).executeQuery();
				while (rs.next()) {
					String attrName = rs.getString("attr_name");
					String type = rs.getString("attr_type");
					String inherit = rs.getString("from_class_name");
					String dateType = rs.getString("data_type");
					String prec = rs.getString("prec");
					String scale = rs.getString("scale");
					//					String dataType2 = rs.getString("class_of.class_name");
					String isNull = rs.getString("is_nullable");
					//					int codeSet = rs.getInt("d.code_set"); //unknown
					String defaultValue = rs.getString("default_value");
					//					String subDataType = rs.getString("sub_data_type");

					DBAttribute attr = new DBAttribute();
					attr.setName(attrName);

					if (type.equals("INSTANCE")) { //INSTANCE
						ret.addAttribute(attr);
					} else if (type.equals("CLASS")) {
						ret.addClassAttribute(attr);
					} else {
						attr.setShared(true);
						ret.addAttribute(attr);
					}
					if (inherit != null) {
						attr.setInherit(inherit);
					} else {
						attr.setInherit(tableName);
					}
					if (isNull.equals("YES")) { //null
						attr.setNotNull(false);
					} else {
						attr.setNotNull(true);
					}
					attr.setDefault(defaultValue);

					if (dateType.equals("CHAR")
							|| dateType.equals("STRING") //|| dateType.equals("VARCHAR")
							|| dateType.equals("BIT")
							|| dateType.equals("VARBIT")
							|| dateType.equals("NCHAR")
							|| dateType.equals("VARNCHAR")) {
						attr.setType(dateType + "(" + prec + ")");
					} else if (dateType.equals("NUMERIC")) {
						attr.setType(dateType + "(" + prec + "," + scale + ")");
					} else {
						attr.setType(dateType);
					}
				}
				try {
					rs.close();
					stmt.close();
				} catch (SQLException ignore) {
				}
			}
			// get auto increment information from db_serial table, which is a system table accessed by all users
			if (ret != null) {
				List<SerialInfo> serialInfoList = new ArrayList<SerialInfo>();
				sql = "select name,owner.name,current_val,increment_val,max_val,min_val,cyclic,started,class_name,att_name "
						+ "from db_serial where class_name =?";
				stmt = connection.prepareStatement(sql);
				((PreparedStatement) stmt).setString(1, tableName);
				rs = ((PreparedStatement) stmt).executeQuery();
				while (rs.next()) {
					String name = rs.getString("name");
					String owner = rs.getString("owner.name");
					String currentVal = rs.getString("current_val");
					String incrementVal = rs.getString("increment_val");
					String maxVal = rs.getString("max_val");
					String minVal = rs.getString("min_val");
					String cyclic = rs.getString("cyclic");
					String startVal = rs.getString("started");
					String className = rs.getString("class_name");
					String attName = rs.getString("att_name");
					boolean isCycle = false;
					if (cyclic != null && cyclic.equals("1")) {
						isCycle = true;
					}
					SerialInfo serialInfo = new SerialInfo(name, owner,
							currentVal, incrementVal, maxVal, minVal, isCycle,
							startVal, className, attName);
					serialInfoList.add(serialInfo);
				}
				for (SerialInfo autoIncrement : serialInfoList) {
					String attrName = autoIncrement.getAttName();
					assert (null != attrName);
					DBAttribute a = ret.getDBAttributeByName(attrName, false);
					if (a != null) {
						a.setAutoIncrement(autoIncrement);
					}
				}
				try {
					rs.close();
					stmt.close();
				} catch (SQLException ignore) {
				}
			}
			//get set(object) type information from db_attr_setdomain_elm view
			if (ret != null) {
				sql = "SELECT a.attr_name, a.attr_type,"
						+ " a.data_type, a.prec, a.scale"
						+ " FROM db_attr_setdomain_elm a"
						+ " WHERE a.class_name=? ";
				stmt = connection.prepareStatement(sql);
				((PreparedStatement) stmt).setString(1, tableName);
				rs = ((PreparedStatement) stmt).executeQuery();
				while (rs.next()) {
					String attrName = rs.getString("attr_name");
					String type = rs.getString("attr_type");
					String dateType = rs.getString("data_type");
					String prec = rs.getString("prec");
					String scale = rs.getString("scale");

					DBAttribute attr = null;
					if (type.equals("INSTANCE")) { //INSTANCE
						attr = ret.getDBAttributeByName(attrName, false);
					} else if (type.equals("CLASS")) {
						attr = ret.getDBAttributeByName(attrName, true);
					} else {
						attr = ret.getDBAttributeByName(attrName, false);
					}

					String subType = null;
					if (dateType.equals("CHAR")
							|| dateType.equals("STRING") //|| dateType.equals("VARCHAR")
							|| dateType.equals("BIT")
							|| dateType.equals("VARBIT")
							|| dateType.equals("NCHAR")
							|| dateType.equals("VARNCHAR")) {
						subType = dateType + "(" + prec + ")";
					} else if (dateType.equals("NUMERIC")) {
						subType = dateType + "(" + prec + "," + scale + ")";
					} else {
						subType = dateType;
					}
					attr.setType(attr.getType() + "(" + subType + ")");
				}
				try {
					rs.close();
					stmt.close();
				} catch (SQLException ignore) {
				}
			}
			//get pk, fk, index(unique,reverse index, reverse unique)
			if (ret != null) {
				sql = "SELECT index_name, is_unique, is_reverse, is_primary_key, is_foreign_key,key_count"
						+ " FROM db_index where class_name=?";
				stmt = connection.prepareStatement(sql);
				((PreparedStatement) stmt).setString(1, tableName);
				rs = ((PreparedStatement) stmt).executeQuery();
				while (rs.next()) {
					String constraintName = rs.getString("index_name");
					String pk = rs.getString("is_primary_key");
					String fk = rs.getString("is_foreign_key");
					String unique = rs.getString("is_unique");
					String reverse = rs.getString("is_reverse");
					int keyCount = rs.getInt("key_count");
					Constraint c = new Constraint();
					c.setName(constraintName);
					if (pk.equals("YES")) {
						c.setType(ConstraintType.PRIMARYKEY.getText());
					} else if (fk.equals("YES")) {
						c.setType(ConstraintType.FOREIGNKEY.getText());
					} else {
						if (unique.equals("NO") && reverse.equals("NO")) {
							c.setType(ConstraintType.INDEX.getText());
						} else if (unique.equals("YES") && reverse.equals("NO")) {
							c.setType(ConstraintType.UNIQUE.getText());
						} else if (unique.equals("NO") && reverse.equals("YES")) {
							c.setType(ConstraintType.REVERSEINDEX.getText());
						} else if (unique.equals("YES")
								&& reverse.equals("YES")) {
							c.setType(ConstraintType.REVERSEUNIQUE.getText());
						}
					}
					c.setKeyCount(keyCount);
					ret.addConstraint(c);
				}
				try {
					rs.close();
					stmt.close();
				} catch (SQLException ignore) {
				}

				List<Constraint> cList = ret.getConstraints();
				for (Constraint c : cList) {
					String constraintName = c.getName();
					sql = "SELECT key_attr_name, asc_desc, key_order  FROM db_index_key"
							+ " where index_name=? and class_name=? order by key_order";
					stmt = connection.prepareStatement(sql);
					((PreparedStatement) stmt).setString(1, constraintName);
					((PreparedStatement) stmt).setString(2, tableName);
					rs = ((PreparedStatement) stmt).executeQuery();
					while (rs.next()) {
						String attrName = rs.getString("key_attr_name");
						String ascDesc = rs.getString("asc_desc");
						c.addAttribute(attrName);
						c.addRule(attrName + " " + ascDesc);
					}
				}
			}
			schema = ret;
			connection.close();
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return;
	}

	public SchemaInfo getSchema() {
		return schema;
	}
}
