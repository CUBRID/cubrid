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
		return broker_name.compareTo(((CASItem)obj).broker_name);
	}
}
