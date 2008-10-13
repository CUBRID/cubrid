package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.ApplicationWorkbenchWindowAdvisor;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.diag.DiagSiteDiagData;
import cubridmanager.diag.view.DiagView;

public class DisconnectAction extends Action {

	public DisconnectAction(String text, IWorkbenchWindow window) {
		super(text);
		setId("DisconnectAction");
		setActionDefinitionId("DisconnectAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/disconnect.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/disconnect.png"));
	}

	public void run() {
		// Delete all view
		for (int i = 0; i < MainRegistry.diagSiteDiagDataList.size(); i++) {
			DiagSiteDiagData diagSiteData = (DiagSiteDiagData) MainRegistry.diagSiteDiagDataList
					.get(i);
			if (diagSiteData.site_name.equals(MainRegistry.HostDesc))
				MainRegistry.diagSiteDiagDataList.remove(i);
		}

		// Environment initial
		if (MainRegistry.soc != null)
			MainRegistry.soc.stoploop();
		MainRegistry.IsConnected = false;
		MainRegistry.HostAddr = null;
		MainRegistry.HostPort = 0;
		MainRegistry.UserID = null;
		MainRegistry.HostJSPort = 0;
		MainRegistry.DiagAuth = MainConstants.AUTH_NONE;
		MainRegistry.CASAuth = MainConstants.AUTH_NONE;
		MainRegistry.Authinfo.clear();
		MainRegistry.IsSecurityManager = false;
		MainRegistry.NaviDraw_CUBRID = false;
		MainRegistry.NaviDraw_CAS = false;
		MainRegistry.NaviDraw_DIAG = false;
		MainRegistry.IsDBAAuth = false;
		MainRegistry.IsCASStart = false;
		MainRegistry.IsCASinfoReady = false;
		MainRegistry.CASinfo.clear();
		ApplicationWorkbenchWindowAdvisor.myconfigurer.setTitle(Messages
				.getString("TITLE.CUBRIDMANAGER"));

		CubridView.removeAll();
		CASView.removeAll();
		DiagView.removeAll();
		// Window refresh
		WorkView.DeleteViewAll();
		CubridView.source.dispose();
		CubridView.types = null;
		try {
			WorkView.TopView(CubridView.ID);
			CubridView.myNavi.setFocus();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}
	}

}
