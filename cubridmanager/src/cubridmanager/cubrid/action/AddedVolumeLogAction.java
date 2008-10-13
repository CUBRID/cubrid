package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.cubrid.dialog.AUTOADDVOL_LOGDialog;

public class AddedVolumeLogAction extends Action {
	public AddedVolumeLogAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("AddedVolumeLogAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		AUTOADDVOL_LOGDialog dlg = new AUTOADDVOL_LOGDialog(shell);
		dlg.doModal();
	}
}
