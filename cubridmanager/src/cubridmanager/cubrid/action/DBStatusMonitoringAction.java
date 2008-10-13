package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.cubrid.dialog.JOB_SETDBSTATUSDialog;

public class DBStatusMonitoringAction extends Action {
	public DBStatusMonitoringAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DBStatusMonitoringAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		JOB_SETDBSTATUSDialog dlg = new JOB_SETDBSTATUSDialog(shell);
		dlg.doModal();
	}
}
