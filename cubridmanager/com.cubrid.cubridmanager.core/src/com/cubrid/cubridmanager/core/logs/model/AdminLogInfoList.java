/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

package com.cubrid.cubridmanager.core.logs.model;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.IModel;

/**
 * 
 * This class is responsible to store all admin log information
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class AdminLogInfoList implements IModel {

	private List<LogInfo> adminLogInfoList = null;

	/**
	 * The constructor
	 */
	public AdminLogInfoList() {
		adminLogInfoList = new ArrayList<LogInfo>();
	}

	/**
	 * get task name.
	 * 
	 * @return
	 */
	public String getTaskName() {
		return "getadminloginfo";
	}

	/**
	 * add log informaton to the adminLogInfo List.
	 * 
	 * @param logInfo
	 */
	public void addAdminLogInfo(LogInfo logInfo) {
		if (adminLogInfoList == null) {
			adminLogInfoList = new ArrayList<LogInfo>();
		}
		if (!adminLogInfoList.contains(logInfo)) {
			adminLogInfoList.add(logInfo);
		}
	}

	/**
	 * get the admin log information List.
	 * 
	 * @return
	 */
	public List<LogInfo> getAdminLogInfoList() {
		return this.adminLogInfoList;
	}
}
