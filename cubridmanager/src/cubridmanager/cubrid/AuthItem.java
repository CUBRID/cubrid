package cubridmanager.cubrid;

import java.util.ArrayList;

public class AuthItem implements Comparable {
	public boolean lock = false;
	public String dbname = null;
	public String dbuser = null;
	public String dbdir = null;
	public byte status = 0;
	public boolean setinfo = false;
	public int pagesize = 0;
	public double freespace = 0.0;
	public ArrayList Volinfo = new ArrayList();
	public ArrayList UserInfo = new ArrayList();
	public ArrayList Schema = new ArrayList();
	public ArrayList JobInfo = new ArrayList();
	public ArrayList AutoQueryInfo = new ArrayList();
	public ArrayList TriggerInfo = new ArrayList();
	public ArrayList LDBInfo = new ArrayList();
	public ArrayList LogInfo = new ArrayList();
	public ArrayList ParaInfo = new ArrayList();
	public boolean isDBAGroup = false;

	public AuthItem(String name, String user, byte stat) {
		dbname = new String(name);
		dbuser = new String(user);
		dbdir = new String("");
		status = stat;
		lock = false;
	}

	public AuthItem(String name, String user, String dir, byte stat) {
		dbname = new String(name);
		dbuser = new String(user);
		dbdir = new String(dir);
		status = stat;
		lock = false;
	}

	public AuthItem(String name, String user, String dir, byte stat,
			boolean dbaGroup) {
		this(name, user, dir, stat);
		isDBAGroup = dbaGroup;
	}

	public int compareTo(Object obj) {
		return dbname.compareTo(((AuthItem) obj).dbname);
	}
}
