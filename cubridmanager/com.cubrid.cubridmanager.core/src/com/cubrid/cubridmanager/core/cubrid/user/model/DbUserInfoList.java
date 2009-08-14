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

package com.cubrid.cubridmanager.core.cubrid.user.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.IModel;

public class DbUserInfoList implements IModel {

	private String dbname;
	private List<DbUserInfo> userList;
	
	private Map<String ,DbUserInfo> userMap;
	
	public String getTaskName() {
		return "userinfo";
	}

	public String getDbname() {
		return dbname;
	}

	public void setDbname(String dbname) {
		this.dbname = dbname;
	}

	public  List<DbUserInfo> getUserList() {
		return userList;
	}

	public synchronized void addUser(DbUserInfo user) {
		if (userList == null)
			userList = new ArrayList<DbUserInfo>();
		if (!userList.contains(user))
			userList.add(user);
	}

	public synchronized void removeUser(DbUserInfo user) {
		if (userList != null)
			userList.remove(user);
	}
	public synchronized Map<String, DbUserInfo> getUserMap() {
		if (userList == null || userList.size() == 0)
			return new HashMap<String, DbUserInfo>();
		if (userMap == null)
			userMap = new HashMap<String, DbUserInfo>();
		for (DbUserInfo bean : userList) {
			if (!userMap.containsKey(bean.getName()))
				userMap.put(bean.getName(), bean);
		}
		return userMap;
	}
}
