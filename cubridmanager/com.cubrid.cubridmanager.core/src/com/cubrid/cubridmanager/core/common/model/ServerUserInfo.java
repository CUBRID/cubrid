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
package com.cubrid.cubridmanager.core.common.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DbCreateAuthType;

/**
 * 
 * This class is responsible to cache CUBRID Manager server user information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ServerUserInfo {
	private String userName = null;
	private String password = null;
	private CasAuthType casAuth = CasAuthType.AUTH_NONE;
	private StatusMonitorAuthType statusMonitorAuth = StatusMonitorAuthType.AUTH_NONE;
	private DbCreateAuthType dbCreateAuthType = DbCreateAuthType.AUTH_NONE;
	// All databases that this user can access
	private List<DatabaseInfo> authDatabaseInfoList = null;

	/**
	 * The constructor
	 */
	public ServerUserInfo() {

	}

	/**
	 * The constructor
	 * 
	 * @param userName
	 * @param password
	 */
	public ServerUserInfo(String userName, String password) {
		this.userName = userName;
		this.password = password;
	}

	/**
	 * @param cmUser
	 * @param cmPassword
	 * @param casAuth
	 * @param isDBAAuth
	 */
	public ServerUserInfo(String userName, String password,
			CasAuthType casAuth, DbCreateAuthType dbCreateAuthType,
			StatusMonitorAuthType statusMonitorAuth) {
		super();
		this.userName = userName;
		this.password = password;
		this.casAuth = casAuth;
		this.dbCreateAuthType = dbCreateAuthType;
		this.statusMonitorAuth = statusMonitorAuth;
	}

	public String getUserName() {
		return userName;
	}

	public void setUserName(String userName) {
		this.userName = userName;
	}

	public String getPassword() {
		return password;
	}

	public void setPassword(String password) {
		this.password = password;
	}

	public CasAuthType getCasAuth() {
		return casAuth;
	}

	public void setCasAuth(CasAuthType casAuth) {
		this.casAuth = casAuth;
	}

	public DbCreateAuthType getDbCreateAuthType() {
		return dbCreateAuthType;
	}

	public void setDbCreateAuthType(DbCreateAuthType dbCreateAuthType) {
		this.dbCreateAuthType = dbCreateAuthType;
	}

	public List<DatabaseInfo> getDatabaseInfoList() {
		return authDatabaseInfoList;
	}

	public void setDatabaseInfoList(List<DatabaseInfo> databaseInfoList) {
		this.authDatabaseInfoList = databaseInfoList;
	}

	public void addDatabaseInfo(DatabaseInfo databaseInfo) {
		if (authDatabaseInfoList == null)
			authDatabaseInfoList = new ArrayList<DatabaseInfo>();
		if (!authDatabaseInfoList.contains(databaseInfo)) {
			authDatabaseInfoList.add(databaseInfo);
		}
	}

	public void removeDatabaseInfo(DatabaseInfo databaseInfo) {
		if (authDatabaseInfoList != null)
			authDatabaseInfoList.remove(databaseInfo);
	}

	public void removeAllDatabaseInfo() {
		if (authDatabaseInfoList != null)
			authDatabaseInfoList.clear();
	}

	public DatabaseInfo getDatabaseInfo(String dbName) {
		for (int i = 0; authDatabaseInfoList != null
				&& i < authDatabaseInfoList.size(); i++) {
			DatabaseInfo databaseInfo = authDatabaseInfoList.get(i);
			if (databaseInfo.getDbName().equalsIgnoreCase(dbName)) {
				return databaseInfo;
			}
		}
		return null;
	}

	public StatusMonitorAuthType getStatusMonitorAuth() {
		return statusMonitorAuth;
	}

	public void setStatusMonitorAuth(StatusMonitorAuthType statusMonitorAuth) {
		this.statusMonitorAuth = statusMonitorAuth;
	}

	public boolean isAdmin() {
		if (userName != null && userName.equals("admin"))
			return true;
		else
			return false;
	}

}
