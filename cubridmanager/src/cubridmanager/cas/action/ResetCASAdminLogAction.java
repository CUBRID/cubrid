package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cas.view.CASLogs;

public class ResetCASAdminLogAction extends Action {
	public ResetCASAdminLogAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ResetCASAdminLogAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (CASLogs.fileinfo == null)
			return;
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.RESETCASADMIN")) != SWT.YES)
			return;

		Shell shell = new Shell();

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "path:" + CASLogs.fileinfo.path,
				"resetlog", Messages.getString("WAIT.RESETCASADMIN"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getadminloginfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		CASView.myNavi.createModel();
		CASView.viewer.refresh();
		WorkView.SetView(CASLogs.ID, CASLogs.CurrentObj, CASLogs.ADMINOBJ);
	}
}
