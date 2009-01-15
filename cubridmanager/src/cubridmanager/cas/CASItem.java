/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package cubridmanager.cas;

import java.util.ArrayList;

import cubridmanager.CommonTool;
import cubridmanager.MainConstants;

public class CASItem implements Comparable {
	public String broker_name=null;
	public int broker_port=0,ASmin=0,ASmax=0,ASnum=0;
	public String apShmId = null;
	public byte status=0;
	public String type=null;
	public String state=null;
	public String pid=null;
	public String port=null;
	public String as=null;
	public String jq=null;
	public String thr=null;
	public String cpu=null;
	public String time=null;
	public String req=null;
	public String auto=null;
	public String ses=null;
	public String sqll=null;
	public String log=null;
	public boolean bSource_env=false;
	public boolean bAccess_list=false;
	public ArrayList loginfo=new ArrayList();

	public CASItem(String p_shmId, String p_name, String p_type, String p_state, String p_pid,
	     String p_port, String p_as, String p_jq, String p_thr,
	     String p_cpu, String p_time, String p_req, String p_auto,
	     String p_ses, String p_sqll, String p_log, boolean p_Source_env, boolean p_Access_list)
	{
		broker_name=new String(p_name);
        type=new String(p_type);
        state=new String(p_state);
        apShmId = new String(p_shmId);
		status=MainConstants.STATUS_NONE;
        if (state.equals("OFF")) {
        	status=MainConstants.STATUS_STOP;
            pid="";
            port="";
            as="";
            jq="";
            thr="";
            cpu="";
            time="";
            req="";
            auto="";
            ses="";
            sqll="";
            log="";
            broker_port=0;
        }
        else {
	        pid=new String(p_pid);
	        port=new String(p_port);
	        as=new String(p_as);
	        jq=new String(p_jq);
	        thr=new String(p_thr);
	        cpu=new String(p_cpu);
	        time=new String(p_time);
	        req=new String(p_req);
	        auto=new String(p_auto);
	        ses=new String(p_ses);
	        sqll=new String(p_sqll);
	        log=new String(p_log);
			broker_port=CommonTool.atoi(port);
			bSource_env=p_Source_env;
			bAccess_list=p_Access_list;
			if (state.equals("ON")) status=MainConstants.STATUS_START;
			else if (state.equals("SUSPENDED") || 
					state.equals("SUSPEND")) status=MainConstants.STATUS_WAIT;
        }
	}

	public int compareTo(Object obj) {
		return this.broker_port-((CASItem)obj).broker_port;
	}
}
