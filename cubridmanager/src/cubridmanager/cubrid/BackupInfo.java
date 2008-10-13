package cubridmanager.cubrid;

public class BackupInfo {
	public String Level = null;
	public String Path = null;
	public String Size = null;
	public String Date = null;

	public BackupInfo(String level, String path, String size, String date) {
		Level = new String(level);
		Size = new String(size);
		Date = new String(date);
		Path = new String(path);
	}
}
