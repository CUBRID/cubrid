package cubridmanager;

public class ProtegoReadCert {
	native String[] selectCert();

	/*
	 * return : String array or null 
	 *          String[0] : User DN 
	 *          String[1] : Signed Data
	 */

	static {
		System.loadLibrary("cub_readcert");
	}

	public String[] protegoSelectCert() {
		return selectCert();
	}

	public ProtegoReadCert() {
	}
}
