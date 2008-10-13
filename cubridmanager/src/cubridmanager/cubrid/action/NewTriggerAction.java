package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.cubrid.dialog.CREATE_TRIGGERDialog;
import cubridmanager.cubrid.view.CubridView;

public class NewTriggerAction extends Action {
	public NewTriggerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("NewTriggerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		CREATE_TRIGGERDialog dlg = new CREATE_TRIGGERDialog(shell);
		if (dlg.doModal()) {
			CubridView.myNavi.createModel();
			CubridView.viewer.refresh();
		}
	}
}
