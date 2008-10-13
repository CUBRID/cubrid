package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.cubrid.dialog.AUTOBACKUP_ERRORDialog;

public class AutoBackupErrorLogAction extends Action {
	public AutoBackupErrorLogAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("AutoBackupErrorLogAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		AUTOBACKUP_ERRORDialog dlg = new AUTOBACKUP_ERRORDialog(shell);
		dlg.doModal();
	}
}
