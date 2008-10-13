package cubridmanager.cubrid;

public class LockTran {
	public String index;
	public String pname;
	public String uid;
	public String host;
	public String pid;
	public String isolevel;
	public String timeout;

	public LockTran(String pindex, String ppname, String puid, String phost,
			String ppid, String pisolevel, String ptimeout) {
		index = pindex;
		pname = ppname;
		uid = puid;
		host = phost;
		pid = ppid;
		isolevel = pisolevel;
		timeout = ptimeout;
	}
}
