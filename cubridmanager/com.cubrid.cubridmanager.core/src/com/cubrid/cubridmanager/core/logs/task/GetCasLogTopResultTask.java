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
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogTopResultInfo;

/**
 * 
 * A task that defined the task of "getcaslogtopresult"
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class GetCasLogTopResultTask extends SocketTask {

	public static final String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "filename",
		        "qindex"
		};

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetCasLogTopResultTask(ServerInfo serverInfo) {
		super("getcaslogtopresult", serverInfo, sendMSGItems);
	}

	/**
	 * set the filename.
	 * 
	 * @param param
	 */
	public void setFileName(String param) {
		super.setMsgItem("filename", param);
	}

	/**
	 * set the qindex.
	 * 
	 * @param param
	 */
	public void setQindex(String param) {
		super.setMsgItem("qindex", param);
	}

	/**
	 * get result from the response
	 * 
	 * @return
	 */
	public AnalyzeCasLogTopResultInfo getAnalyzeCasLogTopResultList() {

		TreeNode response = getResponse();
		if (response == null || (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		AnalyzeCasLogTopResultInfo analyzeCasLogTopResultList = new AnalyzeCasLogTopResultInfo();

		if (response != null && response.getValue("logstringlist") != null
		        && response.getValue("logstringlist").equals("start")) {
			String[] results = response.getValues("logstring");
			for (int j = 0; j < results.length; j++) {
				String str = results[j];
				analyzeCasLogTopResultList.addLogString(str);
			}
		}

		return analyzeCasLogTopResultList;
	}
}
