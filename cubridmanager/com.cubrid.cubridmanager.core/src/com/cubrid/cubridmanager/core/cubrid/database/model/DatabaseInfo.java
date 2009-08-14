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
package com.cubrid.cubridmanager.core.cubrid.database.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.QueryPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.ClassTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetSchemaTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.ModelUtil.ConstraintType;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.core.query.QueryOptions;

/**
 * 
 * This class is responsible to cache CUBRID dabase information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DatabaseInfo {

	private String dbName;
	private String dbDir;
	private DbRunningType runningType = DbRunningType.STANDALONE;
	private boolean isLogined = false;
	//database connection information
	private DbUserInfo authLoginedDbUserInfo = null;
	private String brokerPort = null;
	private String brokerIP = null;
	// all trigger
	private List<Trigger> triggerList = null;
	// all classes
	private List<ClassInfo> userTableInfoList = null;
	private List<ClassInfo> userViewInfoList = null;
	private List<ClassInfo> sysTableInfoList = null;
	private List<ClassInfo> sysViewInfoList = null;
	private Map<String, List<ClassInfo>> partitionedTableMap = null;
	// all bakcup plan list
	private List<BackupPlanInfo> backupPlanInfoList = null;
	// all query plan list
	private List<QueryPlanInfo> queryPlanInfoList = null;
	// all database space
	// information(Generic,data,index,temp,archive_log,active_log)
	private DbSpaceInfoList dbSpaceInfoList = null;
	// all database user information
	private DbUserInfoList dbUserInfoList = null;
	//all stored procedured
	private List<SPInfo> spProcedureInfoList = null;
	private List<SPInfo> spFunctionInfoList = null;
	//all serial
	private List<SerialInfo> serialInfoList = null;

	private ServerInfo serverInfo = null;

	// information of all schemas, key=${tablename}
	private Map<String, SchemaInfo> schemaMap = null;
	private String errorMessage = null;

	/**
	 * The constructor
	 * 
	 * @param dbName
	 * @param serverInfo
	 */
	public DatabaseInfo(String dbName, ServerInfo serverInfo) {
		this.dbName = dbName;
		this.serverInfo = serverInfo;
	}

	/**
	 * 
	 * Clear database cached information
	 * 
	 */
	public void clear() {
		dbUserInfoList = null;
		userTableInfoList = null;
		userViewInfoList = null;
		sysViewInfoList = null;
		sysTableInfoList = null;
		partitionedTableMap = null;
		dbSpaceInfoList = null;
		triggerList = null;
		backupPlanInfoList = null;
		queryPlanInfoList = null;
		spProcedureInfoList = null;
		spFunctionInfoList = null;
		serialInfoList = null;
		schemaMap = null;
	}

	/**
	 * 
	 * Get database name
	 * 
	 * @return
	 */
	public String getDbName() {
		return dbName;
	}

	/**
	 * 
	 * Set database name
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		this.dbName = dbName;
	}

	/**
	 * 
	 * Get database dir
	 * 
	 * @return
	 */
	public String getDbDir() {
		return dbDir;
	}

	/**
	 * 
	 * Set database dir
	 * 
	 * @param dbDir
	 */
	public void setDbDir(String dbDir) {
		this.dbDir = dbDir;
	}

	/**
	 * 
	 * Get database running type
	 * 
	 * @return
	 */
	public DbRunningType getRunningType() {
		return runningType;
	}

	/**
	 * 
	 * Set database running type
	 * 
	 * @param runningType
	 */
	public void setRunningType(DbRunningType runningType) {
		this.runningType = runningType;
		clear();
	}

	/**
	 * 
	 * Return whether database is logined
	 * 
	 * @return
	 */
	public boolean isLogined() {
		return isLogined;
	}

	/**
	 * 
	 * Set whether database is logined
	 * 
	 * @param isLogined
	 */
	public void setLogined(boolean isLogined) {
		this.isLogined = isLogined;
		clear();
	}

	/**
	 * 
	 * Get auth logined database user information
	 * 
	 * @return
	 */
	public DbUserInfo getAuthLoginedDbUserInfo() {
		return authLoginedDbUserInfo;
	}

	/**
	 * 
	 * Set auth logined database user information
	 * 
	 * @param authDbUserInfo
	 */
	public void setAuthLoginedDbUserInfo(DbUserInfo authDbUserInfo) {
		this.authLoginedDbUserInfo = authDbUserInfo;
	}

	/**
	 * 
	 * Get all database user list
	 * 
	 * @return
	 */
	public DbUserInfoList getDbUserInfoList() {
		return dbUserInfoList;
	}

	/**
	 * 
	 * Set database user list
	 * 
	 * @param dbUserInfoList
	 */
	public void setDbUserInfoList(DbUserInfoList dbUserInfoList) {
		this.dbUserInfoList = dbUserInfoList;
	}

	/**
	 * 
	 * Add database user information
	 * 
	 * @param dbUserInfo
	 */
	public synchronized void addDbUserInfo(DbUserInfo dbUserInfo) {
		if (dbUserInfoList == null) {
			dbUserInfoList = new DbUserInfoList();
			dbUserInfoList.setDbname(this.dbName);
		}
	}

	/**
	 * 
	 * Remove database user information
	 * 
	 * @param dbUserInfo
	 */
	public synchronized void removeDbUserInfo(DbUserInfo dbUserInfo) {
		if (dbUserInfoList != null)
			dbUserInfoList.removeUser(dbUserInfo);
	}

	/**
	 * 
	 * Get all triggers list
	 * 
	 * @return
	 */
	public List<Trigger> getTriggerList() {
		return triggerList;
	}

	/**
	 * 
	 * Set triggers list
	 * 
	 * @param triggerList
	 */
	public void setTriggerList(List<Trigger> triggerList) {
		this.triggerList = triggerList;
	}

	/**
	 * 
	 * Add trigger
	 * 
	 * @param trigger
	 */
	public synchronized void addTrigger(Trigger trigger) {
		if (triggerList == null) {
			triggerList = new ArrayList<Trigger>();
		}
		if (!triggerList.contains(trigger))
			triggerList.add(trigger);
	}

	/**
	 * 
	 * Get trigger info by name
	 * 
	 * @param name
	 * @return
	 */
	public Trigger getTrigger(String name) {
		if (triggerList != null && triggerList.size() > 0) {
			for (Trigger trigger : triggerList) {
				if (trigger.getName().equals(name)) {
					return trigger;
				}
			}
		}
		return null;
	}

	/**
	 * 
	 * Get class info list
	 * 
	 * @return
	 */
	public List<ClassInfo> getClassInfoList() {
		List<ClassInfo> classInfoList = new ArrayList<ClassInfo>();
		if (userTableInfoList != null) {
			classInfoList.addAll(userTableInfoList);
		}
		if (userViewInfoList != null) {
			classInfoList.addAll(userViewInfoList);
		}
		if (sysTableInfoList != null) {
			classInfoList.addAll(sysTableInfoList);
		}
		if (sysViewInfoList != null) {
			classInfoList.addAll(sysViewInfoList);
		}
		return classInfoList;
	}

	/**
	 * 
	 * Get user table information list
	 * 
	 * @return
	 */
	public List<ClassInfo> getUserTableInfoList() {
		return userTableInfoList;
	}

	/**
	 * 
	 * Set user table information list
	 * 
	 * @param userTableInfoList
	 */
	public void setUserTableInfoList(List<ClassInfo> userTableInfoList) {
		this.userTableInfoList = userTableInfoList;
	}

	/**
	 * 
	 * Get user view information list
	 * 
	 * @return
	 */
	public List<ClassInfo> getUserViewInfoList() {
		return userViewInfoList;
	}

	/**
	 * 
	 * Set user view information list
	 * 
	 * @param userViewInfoList
	 */
	public void setUserViewInfoList(List<ClassInfo> userViewInfoList) {
		this.userViewInfoList = userViewInfoList;
	}

	/**
	 * 
	 * Get system table information list
	 * 
	 * @return
	 */
	public List<ClassInfo> getSysTableInfoList() {
		return sysTableInfoList;
	}

	/**
	 * 
	 * Set system table information list
	 * 
	 * @param sysTableInfoList
	 */
	public void setSysTableInfoList(List<ClassInfo> sysTableInfoList) {
		this.sysTableInfoList = sysTableInfoList;
	}

	/**
	 * 
	 * Get system view information list
	 * 
	 * @return
	 */
	public List<ClassInfo> getSysViewInfoList() {
		return sysViewInfoList;
	}

	/**
	 * 
	 * Set system view information list
	 * 
	 * @param sysViewInfoList
	 */
	public void setSysViewInfoList(List<ClassInfo> sysViewInfoList) {
		this.sysViewInfoList = sysViewInfoList;
	}

	/**
	 * 
	 * Get partitioned table map,key is partitioned table,value is the children
	 * partitioned table of key
	 * 
	 * @return
	 */
	public Map<String, List<ClassInfo>> getPartitionedTableMap() {
		return partitionedTableMap;
	}

	/**
	 * 
	 * Set partitioned table map,key is partitioned table,value is the children
	 * partitioned table of key
	 * 
	 * @param partitionedTableMap
	 */
	public void setPartitionedTableMap(
			Map<String, List<ClassInfo>> partitionedTableMap) {
		this.partitionedTableMap = partitionedTableMap;
	}

	/**
	 * 
	 * Add the children partitioned table of partitioned table
	 * 
	 * @param tableName
	 * @param classInfoList
	 */
	public void addPartitionedTableList(String tableName,
			List<ClassInfo> classInfoList) {
		if (this.partitionedTableMap == null) {
			this.partitionedTableMap = new HashMap<String, List<ClassInfo>>();
		}
		this.partitionedTableMap.put(tableName, classInfoList);
	}

	/**
	 * 
	 * Get backup plan information list
	 * 
	 * @return
	 */
	public List<BackupPlanInfo> getBackupPlanInfoList() {
		return backupPlanInfoList;
	}

	/**
	 * 
	 * Set backup plan information list
	 * 
	 * @param backupPlanInfoList
	 */
	public void setBackupPlanInfoList(List<BackupPlanInfo> backupPlanInfoList) {
		this.backupPlanInfoList = backupPlanInfoList;
	}

	/**
	 * 
	 * Add backup plan information
	 * 
	 * @param backupPlanInfo
	 */
	public synchronized void addBackupPlanInfo(BackupPlanInfo backupPlanInfo) {
		if (backupPlanInfoList == null) {
			backupPlanInfoList = new ArrayList<BackupPlanInfo>();
		}
		if (!backupPlanInfoList.contains(backupPlanInfo))
			backupPlanInfoList.add(backupPlanInfo);
	}

	/**
	 * 
	 * Remove backup plan information
	 * 
	 * @param backupPlanInfo
	 */
	public synchronized void removeBackupPlanInfo(BackupPlanInfo backupPlanInfo) {
		if (backupPlanInfoList != null)
			backupPlanInfoList.remove(backupPlanInfo);
	}

	/**
	 * 
	 * Remove all backup plan information
	 * 
	 */
	public synchronized void removeAllBackupPlanInfo() {
		if (backupPlanInfoList != null)
			backupPlanInfoList.clear();
	}

	/**
	 * 
	 * Get query plan information list
	 * 
	 * @return
	 */
	public List<QueryPlanInfo> getQueryPlanInfoList() {
		return queryPlanInfoList;
	}

	/**
	 * 
	 * Set query plan information list
	 * 
	 * @param queryPlanInfoList
	 */
	public void setQueryPlanInfoList(List<QueryPlanInfo> queryPlanInfoList) {
		this.queryPlanInfoList = queryPlanInfoList;
	}

	/**
	 * 
	 * Add query plan information
	 * 
	 * @param queryPlanInfo
	 */
	public synchronized void addQueryPlanInfo(QueryPlanInfo queryPlanInfo) {
		if (queryPlanInfoList == null) {
			queryPlanInfoList = new ArrayList<QueryPlanInfo>();
		}
		if (!queryPlanInfoList.contains(queryPlanInfo))
			queryPlanInfoList.add(queryPlanInfo);
	}

	/**
	 * 
	 * Remove query plan information
	 * 
	 * @param queryPlanInfo
	 */
	public synchronized void removeQueryPlanInfo(QueryPlanInfo queryPlanInfo) {
		if (queryPlanInfoList != null)
			queryPlanInfoList.remove(queryPlanInfo);
	}

	/**
	 * 
	 * Remove all query plan information
	 * 
	 */
	public synchronized void removeAllQueryPlanInfo() {
		if (queryPlanInfoList != null)
			queryPlanInfoList.clear();
	}

	/**
	 * 
	 * Get database space information list
	 * 
	 * @return
	 */
	public DbSpaceInfoList getDbSpaceInfoList() {
		return dbSpaceInfoList;
	}

	/**
	 * 
	 * Set database space information list
	 * 
	 * @param dbSpaceInfoList
	 */
	public void setDbSpaceInfoList(DbSpaceInfoList dbSpaceInfoList) {
		this.dbSpaceInfoList = dbSpaceInfoList;
	}

	/**
	 * 
	 * Add database space information
	 * 
	 * @param spaceInfo
	 */
	public void addSpaceInfo(DbSpaceInfo spaceInfo) {
		if (dbSpaceInfoList == null) {
			dbSpaceInfoList = new DbSpaceInfoList();
			dbSpaceInfoList.setDbname(dbName);
		}
		dbSpaceInfoList.addSpaceinfo(spaceInfo);
	}

	/**
	 * 
	 * Remove database space information
	 * 
	 * @param spaceInfo
	 */
	public void removeSpaceInfo(DbSpaceInfo spaceInfo) {
		if (dbSpaceInfoList != null) {
			dbSpaceInfoList.removeSpaceinfo(spaceInfo);
		}
	}

	/**
	 * 
	 * Get database stored procedure information list
	 * 
	 * @return
	 */
	public List<SPInfo> getSpInfoList() {
		List<SPInfo> spInfoList = new ArrayList<SPInfo>();
		if (spFunctionInfoList != null) {
			spInfoList.addAll(spFunctionInfoList);
		}
		if (spProcedureInfoList != null) {
			spInfoList.addAll(spProcedureInfoList);
		}
		return spInfoList;
	}

	/**
	 * 
	 * Get database stored procedure of procedure type information list
	 * 
	 * @return
	 */
	public List<SPInfo> getSpProcedureInfoList() {
		return spProcedureInfoList;
	}

	/**
	 * 
	 * Set database stored procedure of procedure type information list
	 * 
	 * @param spProcedureInfoList
	 */
	public synchronized void setSpProcedureInfoList(
			List<SPInfo> spProcedureInfoList) {
		this.spProcedureInfoList = spProcedureInfoList;
	}

	/**
	 * 
	 * Get database stored procedure of function type information list
	 * 
	 * @return
	 */
	public List<SPInfo> getSpFunctionInfoList() {
		return spFunctionInfoList;
	}

	/**
	 * 
	 * Set database stored procedure of function type information list
	 * 
	 * @param spFunctionInfoList
	 */
	public synchronized void setSpFunctionInfoList(
			List<SPInfo> spFunctionInfoList) {
		this.spFunctionInfoList = spFunctionInfoList;
	}

	/**
	 * 
	 * Get serial information list
	 * 
	 * @return
	 */
	public List<SerialInfo> getSerialInfoList() {
		return serialInfoList;
	}

	/**
	 * 
	 * Set serial information list
	 * 
	 * @param serialInfoList
	 */
	public void setSerialInfoList(List<SerialInfo> serialInfoList) {
		this.serialInfoList = serialInfoList;
	}

	/**
	 * 
	 * Get broker port
	 * 
	 * @return
	 */
	public String getBrokerPort() {
		return brokerPort;
	}

	/**
	 * 
	 * Set broker port
	 * 
	 * @param brokerPort
	 */
	public void setBrokerPort(String brokerPort) {
		this.brokerPort = brokerPort;
	}

	/**
	 * get a schema object via table name
	 * 
	 * Note: using delay loading method for large amount number of tables
	 * 
	 * @param tablename
	 * @return
	 */
	public SchemaInfo getSchemaInfo(String tableName) {
		if (null == schemaMap) {
			schemaMap = new HashMap<String, SchemaInfo>();
		}
		SchemaInfo returnSchemaInfo = schemaMap.get(tableName);
		if (null == returnSchemaInfo) {
			errorMessage = null;
			ClassTask task = new ClassTask(serverInfo);
			task.setDbName(getDbName());
			task.setClassName(tableName);
			task.execute();

			if (task.isSuccess()) {
				returnSchemaInfo = task.getSchemaInfo();
			} else {
				errorMessage = task.getErrorMsg();
				return null;
			}
		} else {
			return returnSchemaInfo;
		}
		GetSchemaTask jdbcTask = new GetSchemaTask(this, tableName);
		jdbcTask.execute();
		SchemaInfo returnSchemaInfo2 = jdbcTask.getSchema();
		if (null != returnSchemaInfo && returnSchemaInfo2 != null) {
			resetSchema(returnSchemaInfo, returnSchemaInfo2);
			returnSchemaInfo2 = null;
		}
		if (null != returnSchemaInfo) {
			putSchemaInfo(returnSchemaInfo);
		}
		//		System.out.println(returnSchemaInfo);
		return returnSchemaInfo;
	}

	/**
	 * A private method only for getSchemaInfo(String tableName); to reset
	 * default(or shared) value, data type and auto increment
	 * 
	 * @param baseSchema
	 * @param fixSchema
	 */
	private void resetSchema(SchemaInfo baseSchema, SchemaInfo fixSchema) {
		List<DBAttribute> attributes = fixSchema.getAttributes();
		for (DBAttribute attr : attributes) {
			DBAttribute baseAttr = baseSchema.getDBAttributeByName(
					attr.getName(), false);
			assert (null != baseAttr);
			//reset default(or shared) value
			baseAttr.setDefault(attr.getDefault());
			baseAttr.resetDefault();
			//reset data type of OBJECT
			if (attr.getType().equals("OBJECT")
					&& baseAttr.getType().equals("")) {
				baseAttr.setType(attr.getType());
			}
			//reset auto increment,just for instance columns
			SerialInfo autoIncrement = attr.getAutoIncrement();
			if (autoIncrement != null) {
				baseAttr.setAutoIncrement(autoIncrement);
			}
		}
		attributes = fixSchema.getClassAttributes();
		for (DBAttribute attr : attributes) {
			DBAttribute baseAttr = baseSchema.getDBAttributeByName(
					attr.getName(), true);
			assert (null != baseAttr);
			//reset default value
			baseAttr.setDefault(attr.getDefault());
			baseAttr.resetDefault();
			//reset data type of OBJECT
			if (attr.getType().equals("OBJECT")
					&& baseAttr.getType().equals("")) {
				baseAttr.setType(attr.getType());
			}
		}
		//reset index(unique,reverse index,reverse unique)
		List<Constraint> constraints = fixSchema.getConstraints();
		for (Constraint c : constraints) {
			String type = c.getType();
			if (type.equals(ConstraintType.INDEX.getText())
					|| type.equals(ConstraintType.REVERSEINDEX.getText())
					|| type.equals(ConstraintType.UNIQUE.getText())
					|| type.equals(ConstraintType.REVERSEUNIQUE.getText())) {
				Constraint baseConstraint = baseSchema.getConstraintByName(c.getName());
				baseConstraint.setKeyCount(c.getKeyCount());
				baseConstraint.setAttributes(c.getAttributes());
				baseConstraint.setRules(c.getRules());
			}
		}

	}

	/**
	 * add a schema object to the map. design reason: class name is unique in a
	 * given database so key=${tablename}
	 * 
	 * @param schema
	 */
	public void putSchemaInfo(SchemaInfo schema) {
		assert (null != schema);
		if (null == schemaMap) {
			schemaMap = new HashMap<String, SchemaInfo>();
		}
		String key = schema.getClassname();
		schemaMap.put(key, schema);
	}

	/**
	 * 
	 * Clear schemas
	 * 
	 */
	public void clearSchemas() {
		if (null == schemaMap) {
			return;
		} else {
			schemaMap.clear();
		}
	}

	public String getErrorMessage() {
		return errorMessage;
	}

	public ServerInfo getServerInfo() {
		return serverInfo;
	}

	public void setServerInfo(ServerInfo serverInfo) {
		this.serverInfo = serverInfo;
	}

	public String getBrokerIP() {
		return brokerIP;
	}

	public void setBrokerIP(String brokerIP) {
		this.brokerIP = brokerIP;
	}

	public String getCharSet() {
		return QueryOptions.getCharset(this);
	}

}
