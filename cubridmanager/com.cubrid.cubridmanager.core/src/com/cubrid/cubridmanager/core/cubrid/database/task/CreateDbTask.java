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
package com.cubrid.cubridmanager.core.cubrid.database.task;

import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * This task is responsible to create database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CreateDbTask extends
		SocketTask {

	private static final String[] sendMSGItems = new String[] { "task",
			"token", "dbname", "numpage", "pagesize", "logsize", "genvolpath",
			"logvolpath", "open", "close", "verwrite_config_file" };

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public CreateDbTask(ServerInfo serverInfo) {
		super("createdb", serverInfo, sendMSGItems);
	}

	/**
	 * 
	 * Set database name
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		super.setMsgItem("dbname", dbName);
	}

	/**
	 * 
	 * Set volume page number
	 * 
	 * @param numPage
	 */
	public void setNumPage(String numPage) {
		super.setMsgItem("numpage", numPage);
	}

	/**
	 * 
	 * Set volume page size
	 * 
	 * @param pageSize
	 */
	public void setPageSize(String pageSize) {
		super.setMsgItem("pagesize", pageSize);
	}

	/**
	 * 
	 * Set log volume size
	 * 
	 * @param logSize
	 */
	public void setLogSize(String logSize) {
		super.setMsgItem("logsize", logSize);
	}

	/**
	 * 
	 * Set generic volume path
	 * 
	 * @param volPath
	 */
	public void setGeneralVolumePath(String volPath) {
		super.setMsgItem("genvolpath", volPath);
	}

	/**
	 * 
	 * Set log volume path
	 * 
	 * @param volPath
	 */
	public void setLogVolumePath(String volPath) {
		super.setMsgItem("logvolpath", volPath);
	}

	/**
	 * 
	 * Set extended volume information list
	 * 
	 * @param volList
	 */
	public void setExVolumes(List<Map<String, String>> volList) {
		StringBuilder sb = new StringBuilder("exvol\n");
		for (int i = 0, len = volList.size(); i < len; i++) {
			Map<String, String> map = volList.get(i);
			String volumeName = map.get("0");
			String volumeType = map.get("1");
			String pageNumber = map.get("2");
			String volumePath = map.get("3");
			sb.append(String.format("%s:%s;%s;%s", volumeName, volumeType,
					pageNumber, volumePath));
			if (i != volList.size() - 1) {
				sb.append("\n");
			}
		}
		super.setMsgItem("open", sb.toString());
		super.setMsgItem("close", "exvol");
	}

	/**
	 * 
	 * Set whether overide conf file
	 * 
	 * @param isOverride
	 */
	public void setOverwriteConfigFile(boolean isOverride) {
		super.setMsgItem("overwrite_config_file", StringUtil.YESNO(isOverride));
	}
}
