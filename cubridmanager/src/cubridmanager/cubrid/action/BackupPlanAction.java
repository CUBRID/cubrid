package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.JOB_BACKUPDialog;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;

public class BackupPlanAction extends Action {
	public BackupPlanAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("BackupPlanAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		JOB_BACKUPDialog dlg = new JOB_BACKUPDialog(shell);
		dlg.gubun = "add";
		dlg.dbname = ai.dbname;
		dlg.backuppath = ai.dbdir + CommonTool.getPathSeperator(ai.dbdir) + "backup";
		dlg.doModal();
	}
}
