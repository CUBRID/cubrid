package cubridmanager.action;

import org.eclipse.core.runtime.IProduct;
import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.internal.IWorkbenchHelpContextIds;
import cubridmanager.Messages;
import cubridmanager.dialog.AboutDialog;

public class AboutAction extends Action implements
		ActionFactory.IWorkbenchAction {

	/**
	 * The workbench window; or <code>null</code> if this action has been
	 * <code>dispose</code>d.
	 */
	private IWorkbenchWindow workbenchWindow;

	/**
	 * Creates a new <code>AboutAction</code>.
	 * 
	 * @param window
	 *            the window
	 */
	public AboutAction(IWorkbenchWindow window) {
		if (window == null)
			throw new IllegalArgumentException();

		this.workbenchWindow = window;

		// use message with no fill-in
		IProduct product = Platform.getProduct();
		String productName = null;
		if (product != null)
			productName = product.getName();
		if (productName == null)
			productName = ""; //$NON-NLS-1$
		setText(Messages.getString("MENU.ABOUT"));
		setToolTipText(Messages.getString("TOOL.ABOUT"));
		setId("about"); //$NON-NLS-1$
		setActionDefinitionId("org.eclipse.ui.help.aboutAction"); //$NON-NLS-1$
		window.getWorkbench().getHelpSystem().setHelp(this,
				IWorkbenchHelpContextIds.ABOUT_ACTION);
	}

	/*
	 * (non-Javadoc) Method declared on IAction.
	 */
	public void run() {
		// make sure action is not disposed
		if (workbenchWindow != null)
			new AboutDialog(workbenchWindow.getShell()).open();
	}

	/*
	 * (non-Javadoc) Method declared on ActionFactory.IWorkbenchAction.
	 */
	public void dispose() {
		workbenchWindow = null;
	}
}