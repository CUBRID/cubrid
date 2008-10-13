package cubridmanager.diag;

public class DiagActivityMonitorTemplate {
	public String templateName = null;
	public String desc = null;
	public String filter = null;
	public String targetdb = null;
	public DiagMonitorConfig activity_config = new DiagMonitorConfig();

	public DiagActivityMonitorTemplate() {
		activity_config.init_client_monitor_config();
	}
}
