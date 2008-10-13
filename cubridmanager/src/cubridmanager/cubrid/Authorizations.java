package cubridmanager.cubrid;

public class Authorizations {
	public String className;
	public boolean selectPriv;
	public boolean insertPriv;
	public boolean updatePriv;
	public boolean alterPriv;
	public boolean deletePriv;
	public boolean indexPriv;
	public boolean executePriv;
	public boolean grantSelectPriv;
	public boolean grantInsertPriv;
	public boolean grantUpdatePriv;
	public boolean grantAlterPriv;
	public boolean grantDeletePriv;
	public boolean grantIndexPriv;
	public boolean grantExecutePriv;
	public boolean allPriv;

	public Authorizations(String p_Name, boolean preset) {
		className = new String(p_Name);
		if (preset) {
			selectPriv = true;
			insertPriv = true;
			updatePriv = true;
			alterPriv = true;
			deletePriv = true;
			indexPriv = true;
			executePriv = true;

			grantSelectPriv = true;
			grantInsertPriv = true;
			grantUpdatePriv = true;
			grantAlterPriv = true;
			grantDeletePriv = true;
			grantIndexPriv = true;
			grantExecutePriv = true;

			allPriv = true;
		} else {
			selectPriv = false;
			insertPriv = false;
			updatePriv = false;
			alterPriv = false;
			deletePriv = false;
			indexPriv = false;
			executePriv = false;

			grantSelectPriv = false;
			grantInsertPriv = false;
			grantUpdatePriv = false;
			grantAlterPriv = false;
			grantDeletePriv = false;
			grantIndexPriv = false;
			grantExecutePriv = false;

			allPriv = false;
		}
	}
}
