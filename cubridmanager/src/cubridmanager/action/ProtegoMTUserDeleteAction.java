package cubridmanager.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubridmanager.dialog.PROPAGE_ProtegoMTUserManagementDialog;

public class ProtegoMTUserDeleteAction extends Action {
	public ProtegoMTUserDeleteAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserAddAction");
	}

	public void run() {
		PROPAGE_ProtegoMTUserManagementDialog.refresh();
	}
}
