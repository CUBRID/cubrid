package cubridmanager.cubrid;

import java.util.ArrayList;

public class UnloadInfo {
	public ArrayList schemaDir = null;
	public ArrayList objectDir = null;
	public ArrayList indexDir = null;
	public ArrayList triggerDir = null;
	public ArrayList schemaDate = null;
	public ArrayList objectDate = null;
	public ArrayList indexDate = null;
	public ArrayList triggerDate = null;
	public String dbname = null;

	public UnloadInfo(String db) {
		dbname = new String(db);
		schemaDir = new ArrayList();
		objectDir = new ArrayList();
		indexDir = new ArrayList();
		triggerDir = new ArrayList();

		schemaDate = new ArrayList();
		objectDate = new ArrayList();
		indexDate = new ArrayList();
		triggerDate = new ArrayList();
	}
}
