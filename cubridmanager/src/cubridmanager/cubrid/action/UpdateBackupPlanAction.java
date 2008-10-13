package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.JOB_BACKUPDialog;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.JobAutomation;

public class UpdateBackupPlanAction extends Action {
	public UpdateBackupPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("UpdateBackupPlanAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (JobAutomation.objrec == null)
			return;
		Shell shell = new Shell(Application.mainwindow.getShell());
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		JOB_BACKUPDialog dlg = new JOB_BACKUPDialog(shell);
		dlg.gubun = "update";
		dlg.dbname = ai.dbname;
		if (dlg.doModal()) {
			WorkView.SetView(JobAutomation.ID, JobAutomation.BACKJOB,
					JobAutomation.ID);
		}
	}
}
