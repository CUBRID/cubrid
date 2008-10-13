package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.WorkView;
import cubridmanager.cas.view.BrokerList;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DatabaseStatus;
import cubridmanager.ApplicationActionBarAdvisor;

public class StartServerAction extends Action {
	public StartServerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("StartServerAction");
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
		String WaitMsg = null;
		String Cmds = null;
		String Msgs = null;

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			if (CubridView.Current_db.length() <= 0)
				return;
			AuthItem authrec = MainRegistry
					.Authinfo_find(CubridView.Current_db);
			if (authrec != null && authrec.status != MainConstants.STATUS_STOP)
				return;
			WaitMsg = Messages.getString("WAIT.CUBRIDSTART");
			authrec.setinfo = false;
			authrec.status = MainConstants.STATUS_START;
			Cmds = "startdb";
			Msgs = "dbname:" + CubridView.Current_db;
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			if (MainRegistry.IsCASStart)
				return;
			WaitMsg = Messages.getString("WAIT.CASSTART");
			Cmds = "startbroker";
			Msgs = "";
			MainRegistry.AddedBrokers.clear();
			MainRegistry.DeletedBrokers.clear();
		} else
			return;

		ClientSocket cs = new ClientSocket();
		if (cs.Connect()) {
			if (!cs.Send(Application.mainwindow.getShell(), Msgs, Cmds)) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(),
						cs.ErrorMsg);
				MainRegistry.IsConnected = false;
			}
		} else {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			MainRegistry.IsConnected = false;
		}
		WaitingMsgBox dlg = new WaitingMsgBox(Application.mainwindow.getShell());
		dlg.run(WaitMsg);
		if (cs.ErrorMsg != null) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			if (MainRegistry.Current_Navigator != MainConstants.NAVI_CUBRID) {
				return;
			}
		}

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			Shell psh = Application.mainwindow.getShell();
			psh.update();
			psh.setEnabled(false);
			psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
			try { // for CAS refresh waiting
				Thread.sleep(2000);
			} catch (Exception e) {
			}
			psh.setEnabled(true);
			psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
		}

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
		}

		Application.mainwindow.getShell().update();
		ApplicationActionBarAdvisor.refreshAction.run();
	}
}
