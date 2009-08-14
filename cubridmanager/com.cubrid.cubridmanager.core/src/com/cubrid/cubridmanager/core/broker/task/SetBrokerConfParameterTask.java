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
package com.cubrid.cubridmanager.core.broker.task;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.ConfComments;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * Set cubrid_broker.conf file content
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-23 created by pangqiren
 */
public class SetBrokerConfParameterTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token", "confdata" };

	/**
	 * @param taskName
	 * @param serverInfo
	 */
	public SetBrokerConfParameterTask(ServerInfo serverInfo) {
		super("broker_setparam", serverInfo, sendedMsgItems);
	}

	public void setConfParameters(
			Map<String, Map<String, String>> confParameters) {
		List<String> confDatas = new ArrayList<String>();
		confDatas.add("");
		confDatas.add("#");
		ConfComments.addComments(confDatas,
				ConfComments.cubrid_copyright_comments);
		confDatas.add("");
		confDatas.add("#");
		//add broker section
		Map<String, String> map = confParameters.get(ConfConstants.broker_sectionName);
		if (map != null) {
			confDatas.add(ConfConstants.broker_section);
			for (int i = 0; i < ConfConstants.brokerParameters.length && i < 2; i++) {
				String key = ConfConstants.brokerParameters[i][0];
				String value = map.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add(key + "=" + value);
				}
			}
		}
		confDatas.add("");
		//add some broker section
		for (Map.Entry<String, Map<String, String>> entry : confParameters.entrySet()) {
			String sectionName = entry.getKey();
			if (sectionName.equals(ConfConstants.broker_sectionName)) {
				continue;
			}
			Map<String, String> brokerParameterMap = entry.getValue();
			if (brokerParameterMap == null) {
				continue;
			}
			if (brokerParameterMap.size() == 0) {
				continue;
			}
			confDatas.add("");
			confDatas.add("[%" + sectionName + "]");
			for (int i = 2; i < ConfConstants.brokerParameters.length; i++) {
				String key = ConfConstants.brokerParameters[i][0];
				String value = brokerParameterMap.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add(key + "=" + value);
				}
			}
		}
		String[] confDataArr = new String[confDatas.size()];
		confDatas.toArray(confDataArr);
		this.setMsgItem("confdata", confDataArr);
	}

}
