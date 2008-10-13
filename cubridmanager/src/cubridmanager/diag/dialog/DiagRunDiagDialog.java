package cubridmanager.diag.dialog;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;

public class DiagRunDiagDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="9,11"
	private Group group = null;
	private Text text = null;
	private Button button = null;
	private Button button1 = null;
	private Button button3 = null;
	private Text text1 = null;
	private Label label = null;
	private Button button5 = null;

	public DiagRunDiagDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagRunDiagDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal(String tName) {
		createSShell();
		sShell.open();
		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(SWT.DIALOG_TRIM);
		sShell.setText("Self test");
		createGroup();
		sShell.setSize(new org.eclipse.swt.graphics.Point(395, 156));
		button5 = new Button(sShell, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(282, 103, 80,
				19));
		button5.setText("\ub2eb\uae30");
		button5.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(sShell, SWT.NONE);
		group.setText("\uc2dc\uac04 \uc124\uc815/\ubcf4\uace0\uc11c");
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(6, 7, 374, 82));
		text = new Text(group, SWT.BORDER);
		text.setBounds(new Rectangle(91, 18, 241, 20));
		button = new Button(group, SWT.NONE);
		button.setBounds(new Rectangle(330, 18, 25, 18));
		button.setText("...");
		button1 = new Button(group, SWT.CHECK);
		button1.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 54, 103, 16));
		button1.setText("\uc2dc\uc791 \uc2dc\uac04 \uc124\uc815");
		button3 = new Button(group, SWT.NONE);
		button3.setBounds(new Rectangle(287, 50, 75, 19));
		button3.setText("\uc9c4\ub2e8 \uc2dc\uc791");
		text1 = new Text(group, SWT.BORDER);
		text1.setBounds(new Rectangle(113, 49, 164, 22));
		label = new Label(group, SWT.NONE);
		label.setBounds(new Rectangle(8, 22, 75, 13));
		label.setText("\ubcf4\uace0\uc11c \uc774\ub984");
	}

}
