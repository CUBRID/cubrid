package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;

import cubridmanager.MainRegistry;
import cubridmanager.TreeObject;
import cubridmanager.TreeParent;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;

public class DBLogout extends Action {
	public DBLogout(String text, String img) {
		super(text);

		setId("DBLogout");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		AuthItem ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		ai.dbuser = "";
		ai.dbdir = "";
		ai.setinfo = false;
		ai.isDBAGroup = false;

		MainRegistry.Authinfo_update(ai);

		CubridView.logoutJob = true;

		TreeParent parent = (TreeParent) CubridView
				.SelectDB(CubridView.Current_db);
		TreeObject[] children = parent.getChildren();
		boolean[] chkTree = new boolean[children.length];
		TreeObject.FindRemove(parent, children, chkTree);

		CubridView.refresh();
	}
}
