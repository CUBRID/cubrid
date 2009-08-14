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
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ClassType;

import cubrid.jdbc.driver.CUBRIDResultSet;

/**
 * 
 * Task of get all tables of the database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class GetAllClassListTask extends
		JDBCTask {
	private String tableName = null;
	private ClassInfo classInfo = null;

	/**
	 * The constructor
	 * 
	 * @param dbInfo
	 *
	 */
	public GetAllClassListTask(DatabaseInfo dbInfo) {
		super("GetAllClassList", dbInfo, false);
	}

	/**
	 * 
	 * Get schema list
	 * 
	 * @param isUserSchema
	 * @param isTable
	 * @return
	 */
	public List<ClassInfo> getSchema(boolean isUserSchema, boolean isTable) {
		List<ClassInfo> allClassInfoList = new ArrayList<ClassInfo>();
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return allClassInfoList;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return allClassInfoList;
			}
			String sql = "select class_name,owner_name,class_type,is_system_class,partitioned from db_class as a where a.class_name not in (select partition_class_name from db_partition)";
			if (isUserSchema) {
				sql += " and is_system_class='NO'";
			} else {
				sql += " and is_system_class='YES'";
			}
			if (isTable) {
				sql += " and class_type='CLASS'";
			} else {
				sql += " and class_type='VCLASS'";
			}
			stmt = connection.createStatement();
			rs = (CUBRIDResultSet) stmt.executeQuery(sql);
			while (rs.next()) {
				String className = rs.getString("class_name");
				String ownerName = rs.getString("owner_name");
				String classType = rs.getString("class_type");
				ClassType type = ClassType.NORMAL;
				if (classType != null
						&& classType.trim().equalsIgnoreCase("VCLASS")) {
					type = ClassType.VIEW;
				}
				String is_system_class = rs.getString("is_system_class");
				boolean isSystemClass = false;
				if (is_system_class != null
						&& is_system_class.trim().equalsIgnoreCase("YES")) {
					isSystemClass = true;
				}
				String partitioned = rs.getString("partitioned");
				boolean isPartitioned = false;
				if (partitioned != null && partitioned.equalsIgnoreCase("YES")) {
					isPartitioned = true;
				}
				ClassInfo classInfo = new ClassInfo(className, ownerName, type,
						isSystemClass, isPartitioned);
				allClassInfoList.add(classInfo);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return allClassInfoList;
	}

	/**
	 * 
	 * Get all class information list
	 * 
	 * @return
	 */
	public List<ClassInfo> getAllClassInfoList() {
		List<ClassInfo> allClassInfoList = new ArrayList<ClassInfo>();
		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return allClassInfoList;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return allClassInfoList;
			}
			String sql = "select class_name,owner_name,class_type,is_system_class,partitioned from db_class where class_name not in (select partition_class_name from db_partition)";
			stmt = connection.createStatement();
			rs = (CUBRIDResultSet) stmt.executeQuery(sql);
			while (rs.next()) {
				String className = rs.getString("class_name");
				String ownerName = rs.getString("owner_name");
				String classType = rs.getString("class_type");
				ClassType type = ClassType.NORMAL;
				if (classType != null
						&& classType.trim().equalsIgnoreCase("VCLASS")) {
					type = ClassType.VIEW;
				}
				String is_system_class = rs.getString("is_system_class");
				boolean isSystemClass = false;
				if (is_system_class != null
						&& is_system_class.trim().equalsIgnoreCase("YES")) {
					isSystemClass = true;
				}
				String partitioned = rs.getString("partitioned");
				boolean isPartitioned = false;
				if (partitioned != null && partitioned.equalsIgnoreCase("YES")) {
					isPartitioned = true;
				}
				ClassInfo classInfo = new ClassInfo(className, ownerName, type,
						isSystemClass, isPartitioned);
				allClassInfoList.add(classInfo);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return allClassInfoList;
	}

	/**
	 * 
	 * Get class information
	 * 
	 */
	public void getClassInfoTaskExcute() {

		try {
			if (errorMsg != null && errorMsg.trim().length() > 0) {
				return;
			}
			if (connection == null || connection.isClosed()) {
				errorMsg = Messages.error_getConnection;
				return;
			}
			if (tableName == null) {
				errorMsg = Messages.error_invalidInput;
				return;
			}
			String sql = "select class_name,owner_name,class_type,is_system_class,partitioned from db_class where class_name=?";
			stmt = connection.prepareStatement(sql);
			((PreparedStatement) stmt).setString(1, tableName);
			rs = (CUBRIDResultSet) ((PreparedStatement) stmt).executeQuery();
			if (rs.next()) {
				String className = rs.getString("class_name");
				String ownerName = rs.getString("owner_name");
				String classType = rs.getString("class_type");
				ClassType type = ClassType.NORMAL;
				if (classType != null
						&& classType.trim().equalsIgnoreCase("VCLASS")) {
					type = ClassType.VIEW;
				}
				String is_system_class = rs.getString("is_system_class");
				boolean isSystemClass = false;
				if (is_system_class != null
						&& is_system_class.trim().equalsIgnoreCase("YES")) {
					isSystemClass = true;
				}
				String partitioned = rs.getString("partitioned");
				boolean isPartitioned = false;
				if (partitioned != null && partitioned.equalsIgnoreCase("YES")) {
					isPartitioned = true;
				}
				classInfo = new ClassInfo(className, ownerName, type,
						isSystemClass, isPartitioned);
			}
		} catch (SQLException e) {
			errorMsg = e.getMessage();
		} finally {
			finish();
		}
		return;
	}

	/**
	 * 
	 * Get table name
	 * 
	 * @return
	 */
	public String getTableName() {
		return tableName;
	}

	public void setTableName(String tableName) {
		this.tableName = tableName.toLowerCase();
	}

	/**
	 * 
	 * Set table name
	 * 
	 * @param classInfo
	 */
	public void setClassInfo(ClassInfo classInfo) {
		this.classInfo = classInfo;
	}

	/**
	 * 
	 * Return class information after task execute
	 * 
	 * @return
	 */
	public ClassInfo getClassInfo() {
		return classInfo;
	}

}
