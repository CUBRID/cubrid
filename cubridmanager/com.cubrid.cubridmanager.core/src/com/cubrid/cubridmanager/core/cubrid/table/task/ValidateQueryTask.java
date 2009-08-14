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
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.Messages;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;

import cubrid.jdbc.driver.CUBRIDResultSetMetaData;

/**
 * 
 * This task is responsible to delete serial
 * 
 * @author robin
 * @version 1.0 - 2009-5-20 created by robin
 */
public class ValidateQueryTask extends
		JDBCTask {

	private List<String> sqls = null;
	private List<Map<String, String>> result = null;

	private CUBRIDResultSetMetaData resultSetMeta;

	private int errorCode = -1;

	public ValidateQueryTask(DatabaseInfo dbInfo) {
		super("CreateFuncProcTask", dbInfo, true);
		result = new ArrayList<Map<String, String>>();
	}

	public void execute() {
		ResultSet rs;
		try {

			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			connection.setAutoCommit(false);
			if (sqls == null)
				return;
			StringBuffer sb = new StringBuffer();
			for (int i = 0; i < sqls.size(); i++) {
				sb.append(sqls.get(i));
				if (i != sqls.size() - 1)
					sb.append(" union all ");
			}
			Statement stmt = connection.createStatement();
			rs = stmt.executeQuery(sb.toString());
			resultSetMeta = (CUBRIDResultSetMetaData) rs.getMetaData();
			for (int i = 1; i < resultSetMeta.getColumnCount() + 1; i++) {
				// "Name", "Data type", "Shared", "Default"
				Map<String, String> map = new HashMap<String, String>();
				String type = resultSetMeta.getColumnTypeName(i);
				if (type != null && type.equalsIgnoreCase("CLASS")) {
					String tableName = resultSetMeta.getTableName(i);
					String colName = resultSetMeta.getColumnName(i);
					DBAttribute bean = getColAttr(tableName, colName);
					if (bean != null && bean.getDomainClassName() != null
							&& !bean.getDomainClassName().equals(""))
						type = bean.getDomainClassName();
					else
						type = "OBJECT";
				}

				map.put("0", resultSetMeta.getColumnName(i));
				map.put("1", type);
				map.put("2", "");
				map.put("3", "");
				map.put("4", "");
				result.add(map);
			}
			rs.close();
			stmt.close();

		} catch (SQLException e) {
			errorCode = e.getErrorCode();
			this.errorMsg = e.getMessage();
		} finally {
			finish();
		}
	}

	public List<String> getSqls() {
		return sqls;
	}

	public void addSqls(String sql) {
		if (sqls == null)
			sqls = new ArrayList<String>();
		this.sqls.add(sql);
	}

	public int getErrorCode() {
		return errorCode;
	}

	public void setErrorCode(int errorCode) {
		this.errorCode = errorCode;
	}

	public List<Map<String, String>> getResult() {
		return result;
	}

	public void setResult(List<Map<String, String>> result) {
		this.result = result;
	}

	private DBAttribute getColAttr(String className, String colName) throws SQLException {
		DBAttribute dbAttribute = null;

		if (errorMsg != null && errorMsg.trim().length() > 0) {
			return dbAttribute;
		}
		if (connection == null || connection.isClosed()) {
			errorMsg = Messages.error_getConnection;
			return dbAttribute;
		}
		String sql = "select attr_name, class_name, attr_type, def_order, from_class_name, from_attr_name, data_type, prec, scale, code_set, domain_class_name, default_value, is_nullable  from db_attribute "
				+ "where class_name= ?" + " and attr_name=? ";
		PreparedStatement stmt = connection.prepareStatement(sql);
		((PreparedStatement) stmt).setString(1, className);
		((PreparedStatement) stmt).setString(2, colName);
		ResultSet rs = stmt.executeQuery();
		if (rs.next()) {
			dbAttribute = new DBAttribute();
			dbAttribute.setName(rs.getString("attr_name"));
			dbAttribute.setType(rs.getString("data_type"));
			String attrType = rs.getString("attr_type");
			String domain_class_name = rs.getString("domain_class_name");
			if (attrType.equalsIgnoreCase("SHARED"))
				dbAttribute.setShared(true);
			else
				dbAttribute.setShared(false);
			dbAttribute.setDomainClassName(domain_class_name);
			dbAttribute.setDefault(rs.getString("default_value"));
			String isNull = rs.getString("is_nullable");
			if (isNull.equalsIgnoreCase("YES"))
				dbAttribute.setNotNull(false);
			else
				dbAttribute.setNotNull(true);

		}
		rs.close();
		stmt.close();
		return dbAttribute;
	}
}
