package cubridmanager.cubrid;

import java.util.ArrayList;

import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.AuthItem;

public class LogFileInfo implements Comparable {
	public String filename;
	public String fileowner;
	public String size;
	public String date;
	public String path;
	public String type = null;

	public LogFileInfo(String p_filename, String p_fileowner, String p_size,
			String p_date, String p_path) {
		filename = new String(p_filename);
		fileowner = new String(p_fileowner);
		size = new String(p_size);
		date = new String(p_date);
		path = new String(p_path);
	}

	public LogFileInfo(String p_type, String p_filename, String p_fileowner,
			String p_size, String p_date, String p_path) {
		type = new String(p_type);
		filename = new String(p_filename);
		fileowner = new String(p_fileowner);
		size = new String(p_size);
		date = new String(p_date);
		path = new String(p_path);
	}

	public static ArrayList DBLogInfo_get(String dbname) {
		AuthItem ai = MainRegistry.Authinfo_find(dbname);
		if (ai == null)
			return null;
		return ai.LogInfo;
	}

	public static ArrayList BrokerLog_get(String brkname) {
		CASItem ci = MainRegistry.CASinfo_find(brkname);
		if (ci == null)
			return null;
		return ci.loginfo;
	}

	public int compareTo(Object obj) {
		return filename.compareTo(((LogFileInfo) obj).filename);
	}
}
