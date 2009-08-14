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
import com.cubrid.cubridmanager.core.logs.model.GetExecuteCasRunnerResultInfo;

/**
 * 
 * A task that defined the task of "executecasrunner"
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class GetExecuteCasRunnerResultTask extends SocketTask {

	public static final String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "dbname",
		        "brokername",
		        "username",
		        "passwd",
		        "num_thread",
		        "repeat_count",
		        "executelogfile",
		        "logfile",
		        "show_queryresult"
		};

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetExecuteCasRunnerResultTask(ServerInfo serverInfo) {
		super("executecasrunner", serverInfo, sendMSGItems);
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
	 * set brokername
	 * 
	 * @param String
	 */
	public void setBrokerName(String param) {
		super.setMsgItem("brokername", param);
	}

	/**
	 * set username
	 * 
	 * @param String
	 */
	public void setUserName(String param) {
		super.setMsgItem("username", param);
	}

	/**
	 * set passwd
	 * 
	 * @param String
	 */
	public void setPasswd(String param) {
		super.setMsgItem("passwd", param);
	}

	/**
	 * set num_thread
	 * 
	 * @param String
	 */
	public void setNumThread(String param) {
		super.setMsgItem("num_thread", param);
	}

	/**
	 * set repeat_count
	 * 
	 * @param String
	 */
	public void setRepeatCount(String param) {
		super.setMsgItem("repeat_count", param);
	}

	/**
	 * set executelogfile
	 * 
	 * @param String
	 */
	public void setExecuteLogFile(String param) {
		super.setMsgItem("executelogfile", param);
	}

	/**
	 * set logfile
	 * 
	 * @param String
	 */
	public void setLogFile(String param) {
		super.setMsgItem("logfile", param);
	}

	/**
	 * set show_queryresult
	 * 
	 * @param String
	 */
	public void setShowQueryResult(String param) {
		super.setMsgItem("show_queryresult", param);
	}

	/**
	 * get result from the response.
	 * 
	 * @return
	 */
	public GetExecuteCasRunnerResultInfo getContent() {
		TreeNode response = getResponse();
		if (response == null || (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		GetExecuteCasRunnerResultInfo getExecuteCasRunnerResultInfo = new GetExecuteCasRunnerResultInfo();
		String queryResultFile = response.getValue("query_result_file");
		String queryResultFileNum = response.getValue("query_result_file_num");

		getExecuteCasRunnerResultInfo.setQueryResultFile(queryResultFile);
		getExecuteCasRunnerResultInfo.setQueryResultFileNum(queryResultFileNum);

		if (response != null && response.getValue("result_list") != null
		        && response.getValue("result_list").equals("start")) {
			String[] results = response.getValues("result");
			if (results != null) {
				for (int j = 0; j < results.length; j++) {
					String str = results[j];
					if (str != null && str.trim().length() > 0) {
						getExecuteCasRunnerResultInfo.addResult(str);
					}
				}
			}
		}

		return getExecuteCasRunnerResultInfo;
	}

}
