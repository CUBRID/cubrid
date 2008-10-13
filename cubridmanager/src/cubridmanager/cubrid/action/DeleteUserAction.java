package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.DBUsers;
import cubridmanager.cubrid.view.CubridView;

public class DeleteUserAction extends Action {
	public DeleteUserAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteUserAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBUsers.Current_select.length() < 1) {
			return;
		}
		if (DBUsers.Current_select.equals("dba")
				|| DBUsers.Current_select.equals("public")) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("ERROR.IDCANNOTBEDELETED"));
			return;
		}
		if (CommonTool.WarnYesNo(DBUsers.Current_select + " "
				+ Messages.getString("WARNYESNO.DELETEUSER")) != SWT.YES) {
			return;
		}
		ClientSocket cs = new ClientSocket();
		String msg = "dbname:" + CubridView.Current_db + "\nusername:"
				+ DBUsers.Current_select;
		if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
				"deleteuser", Messages.getString("WAIT.DELETE"))) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(Application.mainwindow.getShell(), "dbname:"
				+ CubridView.Current_db, "userinfo")) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
		}
		WorkView.DeleteView(DBUsers.ID);
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();

		ApplicationActionBarAdvisor.refreshAction.run();
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			CASView.myNavi.SelectBroker_UpdateView(CASView.Current_broker);
		}
	}
}
