/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.core.cubrid.dbspace.task;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.AddVolumeDbInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAddVolumeStatusInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;



/**
 * Test AddVolumeDbTask
 * 
 * @author cn12978
 * @version 1.0 - 2009-6-24 created by cn12978
 */
public class AddVolumeDbTaskTest extends
		SetupEnvTestCase {
	public void testExecute() {
		String dbName = "demodb";
		GetAddVolumeStatusInfo getAddVolumeStatusInfo = new GetAddVolumeStatusInfo();
		final CommonQueryTask<GetAddVolumeStatusInfo> statusTask = new CommonQueryTask<GetAddVolumeStatusInfo>(
				site,
				CommonSendMsg.commonDatabaseSendMsg, getAddVolumeStatusInfo);
		statusTask.setDbName(dbName);
		statusTask.execute();
		getAddVolumeStatusInfo = statusTask.getResultModel();
		assertNotNull(getAddVolumeStatusInfo);
		String volpath = getAddVolumeStatusInfo.getVolpath();
		assertNotNull(volpath);
		
		AddVolumeDbInfo addVolumeDbInfo = new AddVolumeDbInfo();
	
		addVolumeDbInfo.setDbname(dbName);
		addVolumeDbInfo.setPurpose("generic");
		addVolumeDbInfo.setPath(volpath);
		addVolumeDbInfo.setSize_need_mb("40");
		addVolumeDbInfo.setNumberofpage("10000");
		AddVolumeDbTask addTask = new AddVolumeDbTask(site);
		addTask.setDbname(dbName);
		addTask.setVolname(addVolumeDbInfo.getVolname());
		addTask.setPurpose(addVolumeDbInfo.getPurpose());
		addTask.setPath(addVolumeDbInfo.getPath());
		addTask.setNumberofpages(addVolumeDbInfo.getNumberofpage());
		addTask.setSize_need_mb(addVolumeDbInfo.getSize_need_mb());
		addTask.execute();
		
		assertNull(addTask.getErrorMsg());
	
	 
	}
}
