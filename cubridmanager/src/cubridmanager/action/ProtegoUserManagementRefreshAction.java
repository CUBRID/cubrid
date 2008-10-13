package cubridmanager.action;

import org.eclipse.jface.action.Action;

public class ProtegoUserManagementRefreshAction extends Action {
	public ProtegoUserManagementRefreshAction(String text) {
		super(text);

		setId("ProtegoUserManagementRefreshAction");
		setActionDefinitionId("ProtegoUserManagementRefreshAction");
	}

	public void run() {
		ProtegoUserManagementAction.refresh();
	}
}
