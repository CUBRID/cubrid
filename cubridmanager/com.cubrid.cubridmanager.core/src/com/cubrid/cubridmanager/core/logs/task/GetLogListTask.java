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
package com.cubrid.cubridmanager.core.logs.task;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.logs.model.LogContentInfo;

/**
 * 
 * A task that defined the task of "viewlog"
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class GetLogListTask extends SocketTask {

	public static final String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "path",
		        "start",
		        "end"
		};

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetLogListTask(ServerInfo serverInfo) {
		super("viewlog", serverInfo, sendMSGItems);
	}

	/**
	 * set dbname into msg
	 * 
	 * @param dbName
	 */
	public void setDbName(String dbName) {
		super.setMsgItem("dbname", dbName);
	}

	/**
	 * set path
	 * 
	 * @param String
	 */
	public void setPath(String param) {
		super.setMsgItem("path", param);
	}

	/**
	 * set start
	 * 
	 * @param String
	 */
	public void setStart(String param) {
		super.setMsgItem("start", param);
	}

	/**
	 * set end
	 * 
	 * @param String
	 */
	public void setEnd(String param) {
		super.setMsgItem("end", param);
	}

	/**
	 * get result from the response.
	 * 
	 * @return
	 */
	public LogContentInfo getLogContent() {
		TreeNode response = getResponse();
		if (response == null || (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		LogContentInfo logContentInfo = new LogContentInfo();
		String path = response.getValue("path");
		String total = response.getValue("total");
		if (Integer.parseInt(total) == 0) {
			return null;
		}
		logContentInfo.setPath(path);
		logContentInfo.setTotal(total);
		for (int i = 0; i < response.childrenSize(); i++) {
			TreeNode node = response.getChildren().get(i);
			if (node != null && node.getValue("open") != null && node.getValue("open").equals("log")) {
				String[] lines = node.getValues("line");
				for (int j = 0; j < lines.length; j++) {
					String str = lines[j];
					// if (str != null && str.trim().length() > 0) {
					if (str != null) {
						logContentInfo.addLine(str);
					}
				}
			} else {
				if (node != null) {
					String start = node.getValue("start");
					String end = node.getValue("end");
					if (start != null && start.trim().length() > 0)
						logContentInfo.setStart(start);
					if (end != null && end.trim().length() > 0)
						logContentInfo.setEnd(end);
				}
			}
		}

		return logContentInfo;
	}

}
