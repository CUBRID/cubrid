package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.WorkView;
import cubridmanager.cubrid.dialog.ALTER_TRIGGERDialog;
import cubridmanager.cubrid.view.DBTriggers;

public class AlterTriggerAction extends Action {
	public AlterTriggerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("AlterTriggerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBTriggers.Current_select.length() <= 0)
			return;
		Shell shell = new Shell(Application.mainwindow.getShell());
		ALTER_TRIGGERDialog dlg = new ALTER_TRIGGERDialog(shell);
		if (dlg.doModal()) {
			WorkView.SetView(DBTriggers.ID, DBTriggers.Current_select,
					DBTriggers.ID);
		}
	}
}
