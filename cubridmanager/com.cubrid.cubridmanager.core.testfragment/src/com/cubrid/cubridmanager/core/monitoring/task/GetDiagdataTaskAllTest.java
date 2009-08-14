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

/**
 * TODO: how to write comments
 * The purpose of the class
 * Known bugs
 * The development/maintenance history of the class
 * Document applicable invariants
 * The concurrency strategy
 * 
 * @author lizhiqiang
 * 2009-5-11
 */
public class GetDiagdataTaskAllTest  extends SetupEnvTestCase{
    public void testReceive(){
    //	site.setHostJSPort(monport);
    	GetDiagdataTask task = new GetDiagdataTask(site);
    	task.setDbname("demodb");
    	List <String> list = new ArrayList<String>();
    	String str  = "mon_cub_query_opened_page";
    	list.add(str);
    	 str = "cas_mon_req";
    	list.add(str);
    	str = "cas_mon_tran";
    	list.add(str);
    	str = "cas_mon_act_session";
    	list.add(str);
    	task.buildMsg(list);
    	task.setUsingSpecialDelimiter(true);
    	task.execute();
        task.getResult();
   assertNull(task.getErrorMsg());  
    }
}
