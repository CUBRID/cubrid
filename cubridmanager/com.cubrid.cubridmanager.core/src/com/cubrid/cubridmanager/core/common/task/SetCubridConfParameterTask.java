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
package com.cubrid.cubridmanager.core.common.task;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ConfComments;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;

/**
 * 
 * This task is responsible to set cubrid.conf configuration parameter
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-5 created by pangqiren
 */
public class SetCubridConfParameterTask extends
		SocketTask {
	private static final String[] sendedMsgItems = new String[] { "task",
			"token", "confname", "confdata" };

	/**
	 * @param taskName
	 * @param serverInfo
	 */
	public SetCubridConfParameterTask(ServerInfo serverInfo) {
		super("setsysparam", serverInfo, sendedMsgItems);
		this.setMsgItem("confname", "cubridconf");
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
		confDatas.add("# $Id$");
		confDatas.add("#");
		confDatas.add("# cubrid.conf");
		confDatas.add("#");
		confDatas.add("# For complete information on parameters, see the CUBRID");
		confDatas.add("# Database Administration Guide chapter on System Parameters");
		confDatas.add("");
		//add service section
		Map<String, String> map = confParameters.get(ConfConstants.service_section_name);
		if (map != null) {
			confDatas.add(ConfComments.getComments(ConfConstants.service_section));
			confDatas.add(ConfConstants.service_section);
			confDatas.add("");

			String service = map.get(ConfConstants.service);
			if (service != null && service.trim().length() > 0) {
				ConfComments.addComments(confDatas,
						ConfComments.getComments(ConfConstants.service));
				confDatas.add(ConfConstants.service + "=" + service);
			}
			confDatas.add("");
			String server = map.get(ConfConstants.server);
			if (server != null && server.trim().length() > 0) {
				ConfComments.addComments(confDatas,
						ConfComments.getComments(ConfConstants.server));
				confDatas.add(ConfConstants.server + "=" + server);
			}
		}
		confDatas.add("");
		//add common section
		map = confParameters.get(ConfConstants.common_section_name);
		if (map != null) {
			ConfComments.addComments(confDatas,
					ConfComments.getComments(ConfConstants.common_section));
			confDatas.add(ConfConstants.common_section);
			confDatas.add("");
			for (int i = 0; i < ConfConstants.dbBaseParameters.length; i++) {
				String key = ConfConstants.dbBaseParameters[i][0];
				String value = map.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add("");
					ConfComments.addComments(confDatas,
							ConfComments.getComments(key));
					confDatas.add(key + "=" + value);
				}
			}
			for (int i = 0; i < ConfConstants.dbAdvancedParameters.length; i++) {
				String key = ConfConstants.dbAdvancedParameters[i][0];
				String value = map.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add("");
					ConfComments.addComments(confDatas,
							ConfComments.getComments(key));
					confDatas.add(key + "=" + value);
				}
			}
		}
		//add some database section
		Iterator<Map.Entry<String, Map<String, String>>> it = confParameters.entrySet().iterator();
		while (it.hasNext()) {
			Map.Entry<String, Map<String, String>> entry = it.next();
			String sectionName = entry.getKey();
			if (sectionName.equals(ConfConstants.common_section_name)
					|| sectionName.equals(ConfConstants.service_section_name)) {
				continue;
			}
			Map<String, String> databaseParameterMap = entry.getValue();
			if (databaseParameterMap == null) {
				continue;
			}
			if (databaseParameterMap.size() == 0) {
				continue;
			}
			confDatas.add("");
			confDatas.add(sectionName);
			for (int i = 0; i < ConfConstants.dbBaseParameters.length; i++) {
				String key = ConfConstants.dbBaseParameters[i][0];
				String value = databaseParameterMap.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add("");
					ConfComments.addComments(confDatas,
							ConfComments.getComments(key));
					confDatas.add(key + "=" + value);
				}
			}
			for (int i = 0; i < ConfConstants.dbAdvancedParameters.length; i++) {
				String key = ConfConstants.dbAdvancedParameters[i][0];
				String value = databaseParameterMap.get(key);
				if (value != null && value.trim().length() > 0) {
					confDatas.add("");
					ConfComments.addComments(confDatas,
							ConfComments.getComments(key));
					confDatas.add(key + "=" + value);
				}
			}
		}
		String[] confDataArr = new String[confDatas.size()];
		confDatas.toArray(confDataArr);
		this.setMsgItem("confdata", confDataArr);
	}

}
