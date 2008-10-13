package cubridmanager.cubrid;

public class AddVols {
	public String Volname = null;
	public String Purpose = null;
	public String Pages = null;
	public String Path = null;

	// for addvollogs
	public String DbName = null;
	public String Time = null;
	public String Status = null;

	public AddVols(String vol, String purp, String pages, String path) {
		Volname = new String(vol);
		Purpose = new String(purp);
		Pages = new String(pages);
		Path = new String(path);
		DbName = "";
		Time = "";
		Status = "";
	}

	public AddVols(String db, String vol, String purp, String pages,
			String time, String stat) {
		DbName = new String(db);
		Volname = new String(vol);
		Purpose = new String(purp);
		Pages = new String(pages);
		Time = new String(time);
		Status = new String(stat);
		Path = "";
	}
}
