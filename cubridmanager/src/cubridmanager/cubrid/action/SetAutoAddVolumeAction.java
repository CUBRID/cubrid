package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.dialog.AUTOADDVOLDialog;
import cubridmanager.cubrid.view.CubridView;

public class SetAutoAddVolumeAction extends Action {
	public SetAutoAddVolumeAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SetAutoAddVolumeAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "dbname:" + CubridView.Current_db,
				"getautoaddvol")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		AUTOADDVOLDialog dlg = new AUTOADDVOLDialog(shell);
		dlg.doModal();
	}
}
