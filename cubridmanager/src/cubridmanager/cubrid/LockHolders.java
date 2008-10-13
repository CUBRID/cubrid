package cubridmanager.cubrid;

public class LockHolders {
	public String TranIndex;
	public String GrantMode;
	public String Count;
	public String NSubgranules;

	public LockHolders(String pTranindex, String pGrantMode, String pCount,
			String pNSubgranules) {
		TranIndex = pTranindex;
		GrantMode = pGrantMode;
		Count = pCount;
		NSubgranules = pNSubgranules;
	}
}
