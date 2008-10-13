package cubridmanager.cubrid;

import java.util.ArrayList;

public class LockObject {
	public String entrynum;
	public String maxentrynum;
	public ArrayList entry; // LockEntry

	public LockObject(String pentrynum, String pmaxentrynum) {
		entrynum = pentrynum;
		maxentrynum = pmaxentrynum;
		entry = new ArrayList();
	}
}
