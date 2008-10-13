package cubridmanager;

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;

public class ErrorPage {
	public static boolean needsRefresh = false;
	private static Composite top = null;

	public static void setErrorPage(Composite parent) {
		needsRefresh = true;

		top = new Composite(parent, SWT.NONE);
		top.setBackground(parent.getBackground());
		top.setLayout(new GridLayout(2, false));

		Button btnRefresh = new Button(top, SWT.FLAT);
		btnRefresh.setToolTipText(ApplicationActionBarAdvisor.refreshAction
				.getText());
		btnRefresh.setImage(ApplicationActionBarAdvisor.refreshAction
				.getImageDescriptor().createImage());
		btnRefresh.setBackground(top.getBackground());
		btnRefresh
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ApplicationActionBarAdvisor.refreshAction.run();
					}
				});
		btnRefresh.setLayoutData(new GridData(GridData.GRAB_VERTICAL
				| GridData.VERTICAL_ALIGN_CENTER));

		CLabel errmsg = new CLabel(top, SWT.NONE);
		errmsg.setText(Messages.getString("STRING.ERROR"));
		errmsg.setBackground(top.getBackground());
		errmsg.setFont(new Font(null, top.getFont().toString(), 10, SWT.BOLD));
		errmsg.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}
}
