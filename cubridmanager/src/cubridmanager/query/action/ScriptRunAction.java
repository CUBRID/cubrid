package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.dialog.ActiveDatabaseSelectDialog;
import cubridmanager.query.view.QueryEditor;

public class ScriptRunAction extends Action {
	public ScriptRunAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ScriptRunAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
	}

	public void run() {
		AuthItem aurec = null;
		DBUserInfo selUserInfo = null;

		aurec = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID
				&& CubridView.Current_db.length() > 0) {
			if (aurec.dbuser != null && aurec.dbuser.length() > 0)
				selUserInfo = MainRegistry.getDBUserInfo(aurec.dbname);
			else {
				Shell sh = new Shell(Application.mainwindow.getShell());
				ActiveDatabaseSelectDialog dlg = new ActiveDatabaseSelectDialog(
						sh, CubridView.Current_db);
				selUserInfo = dlg.doModal();
			}
		} else {
			Shell sh = new Shell(Application.mainwindow.getShell());
			ActiveDatabaseSelectDialog dlg = new ActiveDatabaseSelectDialog(sh);
			selUserInfo = dlg.doModal();
		}
		if (selUserInfo == null)
			return;

		FileDialog dialog = new FileDialog(Application.mainwindow.getShell(),
				SWT.OPEN | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		String result = dialog.open();
		if (result != null) {
			WorkView.SetView(QueryEditor.ID, selUserInfo, ":SCRIPTRUN");
			QueryEditor qe = MainRegistry.getCurrentQueryEditor();
			qe.scriptRun(result);
		}
	}
}
