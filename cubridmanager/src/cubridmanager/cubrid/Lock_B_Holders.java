package cubridmanager.cubrid;

public class Lock_B_Holders {
	public String TranIndex;
	public String GrantedMode;
	public String Count;
	public String NSubgranules;
	public String BlockedMode;
	public String StartAt;
	public String WaitForSec;

	public Lock_B_Holders(String pTranIndex, String pGrantedMode,
			String pCount, String pNSubgranules, String pBlockedMode,
			String pStartAt, String pWaitForSec) {
		TranIndex = pTranIndex;
		GrantedMode = pGrantedMode;
		Count = pCount;
		NSubgranules = pNSubgranules;
		BlockedMode = pBlockedMode;
		StartAt = pStartAt;
		WaitForSec = pWaitForSec;
	}
}
