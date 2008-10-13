package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.WorkView;
import cubridmanager.cubrid.view.DBUsers;
import cubridmanager.cubrid.dialog.PROPPAGE_USER_GENERALDialog;

public class UserPropertyAction extends Action {
	public UserPropertyAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("UserPropertyAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		// Information setting
		if (DBUsers.Current_select.length() < 1)
			return;
		PROPPAGE_USER_GENERALDialog.DBUser = DBUsers.Current_select;
		ApplicationActionBarAdvisor.createNewUserAction.runpage(false, this
				.getText());
		WorkView.SetView(DBUsers.ID, DBUsers.Current_select, DBUsers.ID);
	}
}
