package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.CubridView;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.view.JobAutomation;

public class DeleteBackupPlanAction extends Action {
	public DeleteBackupPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteBackupPlanAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (JobAutomation.objrec == null)
			return;
		if (CommonTool.WarnYesNo(Messages
				.getString("WARNYESNO.DELETEBACKUPPLAN")) != SWT.YES)
			return;
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(Application.mainwindow.getShell(), "dbname:"
				+ CubridView.Current_db + "\nbackupid:"
				+ JobAutomation.objrec.backupid, "deletebackupinfo", Messages
				.getString("WAIT.DELETE"))) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendBackGround(Application.mainwindow.getShell(), "dbname:"
				+ CubridView.Current_db, "getbackupinfo", Messages
				.getString("WAITING.GETTINGBACKUPINFO"))) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}
		WorkView.DeleteView(JobAutomation.ID);
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
