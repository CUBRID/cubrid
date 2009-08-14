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
package com.cubrid.cubridmanager.core.monitoring.task;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.monitoring.model.StatusTemplateInfo;
import com.cubrid.cubridmanager.core.monitoring.model.TargetConfigInfo;

/**
 * Test UpdateStatusTemplate
 * 
 * @author lizhiqiang
 * 2009-5-11
 */
public class UpdateStatusTemplateTaskTest  extends SetupEnvTestCase{
    public void testReceive(){
    	
    	StatusTemplateInfo info = new StatusTemplateInfo();
    	info.setDb_name("demodb");
    	info.setDesc("testStatus");
    	info.setName("newStatus");
    	info.setSampling_term("1");
    	
    	
    	AddStatusTemplateTask task = new AddStatusTemplateTask(site);
    	task.setStatusTemplateInfo(info);
    	task.buildMsg();
    	task.execute();
        assertNull(task.getErrorMsg()); 
    	
        info.setDesc("updateStatus");
        TargetConfigInfo targetConfig = new TargetConfigInfo();
        targetConfig.setCas_st_query(new String[]{"red","2.0"});
        info.addTarget_config(targetConfig);
    	UpdateStatusTemplateTask updateTask = new UpdateStatusTemplateTask(site);
		updateTask.setStatusTemplateInfo(info);
		updateTask.buildMsg();
		updateTask.execute();
		assertNull(updateTask.getErrorMsg());
		
		DelStatusTemplateTask delTsk = new DelStatusTemplateTask(site);
    	delTsk.setName("newStatus");
        delTsk.execute();
        assertNull(delTsk.getErrorMsg());
    }
}
