package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.ADD_QUERYPLANDialog;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.JobAutomation;

public class UpdateQueryPlanAction extends Action {
	public UpdateQueryPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("UpdateQueryPlanAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (JobAutomation.objaq == null)
			return;
		Shell shell = new Shell(Application.mainwindow.getShell());
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		ADD_QUERYPLANDialog dlg = new ADD_QUERYPLANDialog(shell);
		dlg.gubun = "update";
		dlg.dbname = ai.dbname;
		if (dlg.doModal()) {
			WorkView.SetView(JobAutomation.ID, JobAutomation.QUERYJOB,
					JobAutomation.ID);
		}
	}
}
