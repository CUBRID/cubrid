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
package com.cubrid.cubridmanager.ui.spi.model;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;

/**
 * 
 * CUBRID Database node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDatabase extends
		DefaultSchemaNode {

	private String startAndLoginIconPath;
	private String startAndLogoutIconPath;
	private String stopAndLoginIconPath;
	private String stopAndLogoutIconPath;

	/**
	 * The constructor
	 * 
	 * @param id
	 * @param label
	 */
	public CubridDatabase(String id, String label) {
		super(id, label, "");
		setType(CubridNodeType.DATABASE);
		setDatabase(this);
		setContainer(true);
	}

	/**
	 * 
	 * Get running type(C/S or standalone) of CUBRID Database
	 * 
	 * @return
	 */
	public DbRunningType getRunningType() {
		return getDatabaseInfo() != null ? getDatabaseInfo().getRunningType()
				: DbRunningType.NONE;
	}

	/**
	 * 
	 * Set CUBRID database running type(C/S or standalone)
	 * 
	 * @param isRunning
	 */
	public void setRunningType(DbRunningType dbRunningType) {
		if (getDatabaseInfo() != null) {
			getDatabaseInfo().setRunningType(dbRunningType);
		}
	}

	/**
	 * 
	 * Get logined user name
	 * 
	 * @return
	 */
	public String getUserName() {
		if (getDatabaseInfo() != null) {
			return getDatabaseInfo().getAuthLoginedDbUserInfo() != null ? getDatabaseInfo().getAuthLoginedDbUserInfo().getName()
					: null;
		}
		return null;
	}

	/**
	 * 
	 * Get logined password
	 * 
	 * @return
	 */
	public String getPassword() {
		if (getDatabaseInfo() != null) {
			return getDatabaseInfo().getAuthLoginedDbUserInfo() != null ? getDatabaseInfo().getAuthLoginedDbUserInfo().getNoEncryptPassword()
					: null;
		}
		return null;
	}

	/**
	 * 
	 * Get whether it has logined
	 * 
	 * @return
	 */
	public boolean isLogined() {
		if (getDatabaseInfo() != null) {
			return getDatabaseInfo().isLogined();
		}
		return false;
	}

	/**
	 * 
	 * Set logined status
	 * 
	 * @param isLogin
	 */
	public void setLogined(boolean isLogined) {
		if (getDatabaseInfo() != null) {
			getDatabaseInfo().setLogined(isLogined);
		}
	}

	/**
	 * Get database information
	 * 
	 * @return
	 */
	public DatabaseInfo getDatabaseInfo() {
		if (this.getAdapter(DatabaseInfo.class) != null)
			return (DatabaseInfo) this.getAdapter(DatabaseInfo.class);
		return null;
	}

	/**
	 * Set database information
	 * 
	 * @param database
	 */
	public void setDatabaseInfo(DatabaseInfo databaseInfo) {
		this.setModelObj(databaseInfo);
	}

	/**
	 * Get icon path of database start and login status
	 */
	public String getStartAndLoginIconPath() {
		return startAndLoginIconPath;
	}

	/**
	 * 
	 * Set icon path of database stop and logout status
	 * 
	 * @param startAndLoginIconPath
	 */
	public void setStartAndLoginIconPath(String startAndLoginIconPath) {
		this.startAndLoginIconPath = startAndLoginIconPath;
	}

	/**
	 * Get icon path of database start and logout status
	 */
	public String getStartAndLogoutIconPath() {
		return startAndLogoutIconPath;
	}

	/**
	 * 
	 * Set icon path of database stop and logout status
	 * 
	 * @param startAndLogoutIconPath
	 */
	public void setStartAndLogoutIconPath(String startAndLogoutIconPath) {
		this.startAndLogoutIconPath = startAndLogoutIconPath;
	}

	/**
	 * Get icon path of database stop and login status
	 */
	public String getStopAndLoginIconPath() {
		return stopAndLoginIconPath;
	}

	/**
	 * 
	 * Set icon path of database stop and logout status
	 * 
	 * @param stopAndLoginIconPath
	 */
	public void setStopAndLoginIconPath(String stopAndLoginIconPath) {
		this.stopAndLoginIconPath = stopAndLoginIconPath;
	}

	/**
	 * Get icon path of database stop and logout status
	 */
	public String getStopAndLogoutIconPath() {
		return stopAndLogoutIconPath;
	}

	/**
	 * 
	 * Set icon path of database stop and logout status
	 * 
	 * @param stopAndLogoutIconPath
	 */
	public void setStopAndLogoutIconPath(String stopAndLogoutIconPath) {
		this.stopAndLogoutIconPath = stopAndLogoutIconPath;
	}

}
