package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class Jobs implements Comparable {
	public String backupid;
	public String path;
	public String periodtype;
	public String perioddetail;
	public String time;
	public String level;
	public String archivedel;
	public String updatestatus;
	public String storeold;
	public String onoff;
	public String zip;
	public String check;
	public String mt;

	public Jobs(String p_backupid, String p_path, String p_periodtype,
			String p_perioddetail, String p_time, String p_level,
			String p_archivedel, String p_updatestatus, String p_storeold,
			String p_onoff, String p_zip, String p_check, String p_mt) {
		backupid = new String(p_backupid);
		path = new String(p_path);
		periodtype = new String(p_periodtype);
		perioddetail = new String(p_perioddetail);
		time = new String(p_time);
		level = new String(p_level);
		archivedel = new String(p_archivedel);
		updatestatus = new String(p_updatestatus);
		storeold = new String(p_storeold);
		onoff = new String(p_onoff);
		zip = new String(p_zip);
		check = new String(p_check);
		mt = new String(p_mt);
	}

	public static ArrayList JobsInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.JobInfo;
	}

	public int compareTo(Object obj) {
		return backupid.compareTo(((Jobs) obj).backupid);
	}
}
