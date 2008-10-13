package cubridmanager.diag;

import java.util.ArrayList;

import cubridmanager.MainRegistry;

public class DiagSiteDiagData {
	public String site_name = null;
	public ArrayList statusTemplateList = new ArrayList();
	public ArrayList activityTemplateList = new ArrayList();
	public ArrayList diagDataActivityLogList = new ArrayList();

	public DiagSiteDiagData() {
		site_name = MainRegistry.HostDesc;
	}
}
