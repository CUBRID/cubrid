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
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;

import cubrid.jdbc.driver.CUBRIDResultSet;

/**
 * 
 * Get all attributes of a given table
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-18 created by pangqiren
 */
public class GetAllAttrTask extends
		JDBCTask {
	private String className = null;
	private List<DBAttribute> allAttrList = null;

	public GetAllAttrTask(DatabaseInfo dbInfo) {
		super("GetAllAttr", dbInfo,  false);
	}

	public List<String> getAllAttrList(String className) {
		List<String> allAttrList = new ArrayList<String>();
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return allAttrList;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return allAttrList;
			}
			String sql = "select attr_name,def_order from db_attribute where  class_name='"
					+ className.toLowerCase() + "' order by def_order";
			stmt = connection.createStatement();
			rs = (CUBRIDResultSet) stmt.executeQuery(sql);
			while (rs.next()) {
				String attrName = rs.getString("attr_name");
				allAttrList.add(attrName);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return allAttrList;
	}

	public void getDbAllAttrListTaskExcute() {
		allAttrList = new ArrayList<DBAttribute>();
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			String sql = "select attr_name, class_name, attr_type, def_order, from_class_name, from_attr_name, data_type, prec, scale, code_set, domain_class_name, default_value, is_nullable  from db_attribute where  class_name="
					+ " ? order by def_order";
			stmt = connection.prepareStatement(sql);
			((PreparedStatement) stmt).setString(1, className.toLowerCase());
			rs = (CUBRIDResultSet) ((PreparedStatement) stmt).executeQuery();
			while (rs.next()) {
				DBAttribute dbAttribute = new DBAttribute();
				dbAttribute.setName(rs.getString("attr_name"));
				dbAttribute.setType(rs.getString("data_type"));
				String attrType = rs.getString("attr_type");
				dbAttribute.setDomainClassName(rs.getString("domain_class_name"));
				if (attrType.equalsIgnoreCase("SHARED")) {
					dbAttribute.setShared(true);
				} else {
					dbAttribute.setShared(false);
				}
				if (attrType.equalsIgnoreCase("CLASS")) {
					dbAttribute.setClassAttribute(true);
				} else {
					dbAttribute.setClassAttribute(false);
				}
				dbAttribute.setDefault(rs.getString("default_value"));
				String isNull = rs.getString("is_nullable");
				if (isNull.equalsIgnoreCase("YES"))
					dbAttribute.setNotNull(false);
				else
					dbAttribute.setNotNull(true);

				allAttrList.add(dbAttribute);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return;
	}

	public String getClassName() {
		return className;
	}

	public void setClassName(String className) {
		this.className = className;
	}

	public List<DBAttribute> getAllAttrList() {
		return allAttrList;
	}

}
