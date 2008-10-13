package cubridmanager.cubrid;

import java.util.ArrayList;

public class LockInfo {
	public String esc;
	public String dlinterval;
	public ArrayList locktran;

	public LockInfo(String pesc, String pdl) {
		esc = pesc;
		dlinterval = pdl;
		locktran = new ArrayList(); // LockTran
	}
}
