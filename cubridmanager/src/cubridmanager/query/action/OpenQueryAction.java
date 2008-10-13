package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
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

public class OpenQueryAction extends Action {
	public OpenQueryAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("OpenQueryEditorFile");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText("TOOLTIP." + text);
	}

	public void run() {
		QueryEditor qe = MainRegistry.getCurrentQueryEditor();
		if (qe == null) {
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
				ActiveDatabaseSelectDialog dlg = new ActiveDatabaseSelectDialog(
						sh);
				selUserInfo = dlg.doModal();
			}
			if (selUserInfo == null)
				return;

			WorkView.SetView(QueryEditor.ID, selUserInfo, null);
			qe = MainRegistry.getCurrentQueryEditor();
		}
		qe.openFile();
	}
}
