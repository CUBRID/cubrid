package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.dialog.ADDVOLDialog;
import cubridmanager.cubrid.view.CubridView;

public class AddVolumeAction extends Action {
	public AddVolumeAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("AddVolumeAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "dbname:" + CubridView.Current_db,
				"getaddvolstatus")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		ADDVOLDialog dlg = new ADDVOLDialog(shell);
		dlg.doModal();
	}
}
