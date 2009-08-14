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
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogResultInfo;
import com.cubrid.cubridmanager.core.logs.model.AnalyzeCasLogResultList;

/**
 * 
 * A task that defined the task of "analyzecaslog"
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-4-3 created by wuyingshi
 */
public class GetAnalyzeCasLogTask extends SocketTask {

	public static final String[] sendMSGItems = new String[]
		{
		        "task",
		        "token",
		        "open",
		        "logfile",
		        "close",
		        "option_t"
		};

	/**
	 * The constructor
	 * 
	 * @param serverInfo
	 */
	public GetAnalyzeCasLogTask(ServerInfo serverInfo) {
		super("analyzecaslog", serverInfo, sendMSGItems);
	}

	/**
	 * set the otion_t value.
	 * 
	 * @param param
	 */
	public void setOptionT(String param) {
		super.setMsgItem("option_t", param);
	}

	/**
	 * set the logFiles values.
	 * 
	 * @param param
	 */
	public void setLogFiles(String[] param) {
		super.setMsgItem("open", "logfilelist");
		super.setMsgItem("logfile", param);
		super.setMsgItem("close", "logfilelist");
	}

	/**
	 * get result from the response.
	 * 
	 * @return
	 */
	public AnalyzeCasLogResultList getAnalyzeCasLogResultList() {

		TreeNode response = getResponse();
		if (response == null || (this.getErrorMsg() != null && getErrorMsg().trim().length() > 0)) {
			return null;
		}
		AnalyzeCasLogResultList analyzeCasLogResultList = new AnalyzeCasLogResultList();
		String resultfile = response.getValue("resultfile");

		analyzeCasLogResultList.setResultfile(resultfile);

		if (response != null && response.getValue("resultlist") != null
		        && response.getValue("resultlist").equals("start")) {
			String[] results = response.getValues("result");
			if (results != null) {
				for (int j = 0; j < results.length / 2; j++) {
					AnalyzeCasLogResultInfo analyzeCasLogResultInfo = new AnalyzeCasLogResultInfo();
					analyzeCasLogResultInfo.setQindex((response.getValues("qindex"))[j]);
					if (response.getValue("max") != null) {
						analyzeCasLogResultInfo.setMax((response.getValues("max"))[j]);
						analyzeCasLogResultInfo.setMin((response.getValues("min"))[j]);
						analyzeCasLogResultInfo.setAvg((response.getValues("avg"))[j]);
						analyzeCasLogResultInfo.setCnt((response.getValues("cnt"))[j]);
						analyzeCasLogResultInfo.setErr((response.getValues("err"))[j]);
					} else {
						analyzeCasLogResultInfo.setExecTime((response.getValues("exec_time"))[j]);
					}
					if (analyzeCasLogResultInfo != null) {
						analyzeCasLogResultList.addResultFile(analyzeCasLogResultInfo);
					}
				}
			}
		}

		return analyzeCasLogResultList;
	}

}
