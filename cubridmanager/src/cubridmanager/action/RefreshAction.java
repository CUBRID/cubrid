package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.ErrorPage;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.view.BrokerJob;
import cubridmanager.cas.view.CASView;
import cubridmanager.diag.view.DiagView;

public class RefreshAction extends Action {
	public RefreshAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("RefreshAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		if (!MainRegistry.IsConnected)
			return;
		Shell psh = Application.mainwindow.getShell();
		psh.setEnabled(false);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(psh, "", "getenv")) {
				CommonTool.ErrorBox(psh, cs.ErrorMsg);
				MainRegistry.IsConnected = false;
			}
			if (!cs.SendClientMessage(psh, "", "startinfo")) {
				CommonTool.ErrorBox(psh, cs.ErrorMsg);
				MainRegistry.IsConnected = false;
			}
			psh.update();

			AuthItem authrec = MainRegistry
					.Authinfo_find(CubridView.Current_db);
			if (authrec != null)
				authrec.setinfo = false;
			CubridView.viewer.refresh();
			CubridView.myNavi.updateWorkView();
			MainRegistry.Current_Navigator = MainConstants.NAVI_CUBRID;

			AuthItem ai = null;
			boolean doesNotLoginAnyDatabase = true;
			for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
				ai = (AuthItem) MainRegistry.Authinfo.get(i);
				if (ai.dbuser != null && ai.dbuser.length() > 0) {
					CubridView.myNavi.GetDBInfo(ai.dbname);
					doesNotLoginAnyDatabase = false;
				}
			}
			if (doesNotLoginAnyDatabase)
				CubridView.refresh();
			ApplicationActionBarAdvisor
					.AdjustToolbar(MainConstants.NAVI_CUBRID);
			if (ErrorPage.needsRefresh) {
				CubridView.setViewDefault();
				ErrorPage.needsRefresh = false;
			}
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(psh, "", "getbrokersinfo")) {
				CommonTool.ErrorBox(psh, cs.ErrorMsg);
				MainRegistry.IsConnected = false;
			} else {
				for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
					// get each broker's information
					CASItem cItem = (CASItem) MainRegistry.CASinfo.get(i);

					// broker status
					ClientSocket bStatCS = new ClientSocket();
					if (!bStatCS.SendClientMessage(psh, "bname:"
							+ cItem.broker_name + "\n", "getbrokerstatus")) {
						CommonTool.ErrorBox(psh, bStatCS.ErrorMsg);
						break;
					}

					// logfile info
					ClientSocket bLogFileCS = new ClientSocket();
					if (!bLogFileCS.SendClientMessage(psh, "broker:"
							+ cItem.broker_name + "\n", "getlogfileinfo")) {
						CommonTool.ErrorBox(psh, bLogFileCS.ErrorMsg);
						break;
					}

					// getaslimit
					ClientSocket bAsLimitCS = new ClientSocket();
					if (!bAsLimitCS.SendClientMessage(psh, "bname:"
							+ cItem.broker_name + "\n", "getaslimit")) {
						CommonTool.ErrorBox(psh, bAsLimitCS.ErrorMsg);
						break;
					}

				}

				psh.update();
				ClientSocket cs2 = new ClientSocket();
				if (!cs2.SendClientMessage(psh, "", "getadminloginfo")) {
					CommonTool.ErrorBox(psh, cs2.ErrorMsg);
				}
			}
			psh.update();
			if (DiagView.myNavi != null) {
				DiagView.myNavi.saveExpandedState();
				DiagView.viewer.setInput(DiagView.myNavi.createModel());
				DiagView.viewer.refresh();
				DiagView.myNavi.restoreExpandedState();
			}

			CASView.myNavi.createModel();
			CASView.viewer.refresh();
			CASView.myNavi.updateWorkView();
			if (CASView.Current_broker.length() > 0) {
				if (CASView.Current_select.equals(BrokerJob.ID))
					WorkView
							.SetView(BrokerJob.ID, CASView.Current_broker, null);
			}
			ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_CAS);
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_DIAG) {
			psh.setEnabled(false);
			psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
			psh.update();

			if (CASView.myNavi != null) {
				CASView.myNavi.createModel();
				CASView.viewer.refresh();
			}
			DiagView.refresh();
			DiagView.myNavi.updateWorkView();

			ApplicationActionBarAdvisor.AdjustToolbar(MainConstants.NAVI_DIAG);
		}

		psh.setEnabled(true);
		psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
	}
}
