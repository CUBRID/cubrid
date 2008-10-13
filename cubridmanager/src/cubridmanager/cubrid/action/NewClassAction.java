package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.cubrid.dialog.CREATE_CLASSDialog;
import cubridmanager.cubrid.view.CubridView;

public class NewClassAction extends Action {
	public static String newclass = "";

	public NewClassAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("NewClassAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		CREATE_CLASSDialog dlg = new CREATE_CLASSDialog(shell);
		if (dlg.doModal()) { // class or view create ok
			CubridView.myNavi.createModel();
			CubridView.viewer.refresh();
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(shell, "dbname:" + CubridView.Current_db
					+ "\nclassname:" + newclass, "class")) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
			DBSchema.CurrentObj = newclass;
			ApplicationActionBarAdvisor.tablePropertyAction.runpage();
		}
	}
}
