package cubridmanager.cubrid;

import java.util.ArrayList;

public class LockEntry {
	public String Oid;
	public String ObjectType;
	public String NumHolders;
	public String Num_B_Holders;
	public String NumWaiters;
	public ArrayList LockHolders;
	public ArrayList Lock_B_Holders;
	public ArrayList LockWaiters;

	public LockEntry(String pOid, String pObjectType, String pNumHolders,
			String pNum_B_Holders, String pNumWaiters) {
		Oid = pOid;
		ObjectType = pObjectType;
		NumHolders = pNumHolders;
		Num_B_Holders = pNum_B_Holders;
		NumWaiters = pNumWaiters;

		LockHolders = new ArrayList();
		Lock_B_Holders = new ArrayList();
		LockWaiters = new ArrayList();
	}

}
