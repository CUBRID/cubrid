package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.WorkView;
import cubridmanager.query.view.QueryEditor;
import cubridmanager.Application;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.dialog.ActiveDatabaseSelectDialog;

public class QueryEditAction extends Action {
	boolean needActiveDataBaseSelectDialog = true;

	public QueryEditAction(String text, String img, boolean needDBSelectDlg) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("QueryEditAction");
		// Associate the action with a pre-defined command, to allow key
		// bindings.
		setActionDefinitionId("QueryEditAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		needActiveDataBaseSelectDialog = needDBSelectDlg;
		setToolTipText(text);
	}

	public void run() {
		AuthItem aurec = null;
		DBUserInfo selUserInfo = null;

		aurec = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (needActiveDataBaseSelectDialog) {
			Shell sh = new Shell();
			ActiveDatabaseSelectDialog dlg = null;
			if (CubridView.Current_db.length() > 0)
				dlg = new ActiveDatabaseSelectDialog(sh, CubridView.Current_db);
			else
				dlg = new ActiveDatabaseSelectDialog(sh);
			selUserInfo = dlg.doModal();
		} else {
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
		}
		if (selUserInfo == null)
			return;
		WorkView.SetView(QueryEditor.ID, selUserInfo, null);
	}

}
