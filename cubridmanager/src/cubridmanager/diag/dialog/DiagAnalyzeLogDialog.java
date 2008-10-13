package cubridmanager.diag.dialog;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.List;

public class DiagAnalyzeLogDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	public String logFileName = "";
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="39,14"
	private TabFolder tabFolder = null;
	private Composite composite = null;
	private Composite composite1 = null;
	private Table table = null;
	private Label label = null;
	private Text textArea = null;
	private Group group = null;
	private Tree tree = null;
	private Tree tree1 = null;
	private Button button = null;
	private Button button1 = null;
	private Group group1 = null;
	private List list = null;
	private Button button2 = null;
	private Button button3 = null;
	private Table table1 = null;
	private Button button5 = null;
	private Group group2 = null;
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Text text = null;
	private Button button6 = null;
	private Button button4 = null;

	public DiagAnalyzeLogDialog(Shell parent, int style) {
		super(parent, style);
	}

	public DiagAnalyzeLogDialog(Shell parent) {
		super(parent);
	}

	public DiagAnalyzeLogDialog(Shell parent, String logName) {
		super(parent);
		logFileName = logName;
	}

	public int doModal() {
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
		sShell.setText("Log analyzer");
		createTabFolder();
		sShell.setSize(new org.eclipse.swt.graphics.Point(476, 500));
		button5 = new Button(sShell, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(337, 441, 91,
				23));
		button5.setText("Close");
		button5
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes tabFolder
	 * 
	 */
	private void createTabFolder() {
		tabFolder = new TabFolder(sShell, SWT.NONE);
		createComposite();
		createComposite1();
		tabFolder.setBounds(new org.eclipse.swt.graphics.Rectangle(9, 11, 444,
				418));
		TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
		tabItem.setText("Analysis result");
		tabItem.setControl(composite);
		TabItem tabItem1 = new TabItem(tabFolder, SWT.NONE);
		tabItem1.setText("Setting");
		tabItem1.setControl(composite1);
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		composite = new Composite(tabFolder, SWT.NONE);
		createTable();
		label = new Label(composite, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(4, 270, 59, 16));
		label.setText("Explanation");
		textArea = new Text(composite, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL);
		textArea.setBounds(new org.eclipse.swt.graphics.Rectangle(8, 286, 417,
				57));
		button4 = new Button(composite, SWT.NONE);
		button4.setBounds(new org.eclipse.swt.graphics.Rectangle(294, 358, 89,
				23));
		button4.setText("Create analysis report");
	}

	/**
	 * This method initializes composite1
	 * 
	 */
	private void createComposite1() {
		composite1 = new Composite(tabFolder, SWT.NONE);
		createGroup();
		createGroup1();
		createGroup2();
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(composite, SWT.NONE);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table.setBounds(new org.eclipse.swt.graphics.Rectangle(0, -1, 439, 259));
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(composite1, SWT.NONE);
		group.setText("Object");
		createTree();
		createTree1();
		group .setBounds(new org.eclipse.swt.graphics.Rectangle(10, 54, 413, 190));
		button = new Button(group, SWT.NONE);
		button .setBounds(new org.eclipse.swt.graphics.Rectangle(174, 74, 63, 24));
		button.setText("Add >");
		button1 = new Button(group, SWT.NONE);
		button1.setBounds(new org.eclipse.swt.graphics.Rectangle(175, 114, 63, 23));
		button1.setText("< Remove");
	}

	/**
	 * This method initializes tree
	 * 
	 */
	private void createTree() {
		tree = new Tree(group, SWT.NONE);
		tree.setBounds(new org.eclipse.swt.graphics.Rectangle(9, 18, 147, 159));
	}

	/**
	 * This method initializes tree1
	 * 
	 */
	private void createTree1() {
		tree1 = new Tree(group, SWT.NONE);
		tree1.setBounds(new org.eclipse.swt.graphics.Rectangle(255, 19, 147,
				159));
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(composite1, SWT.NONE);
		group1.setText("Column");
		group1.setBounds(new org.eclipse.swt.graphics.Rectangle(9, 247, 415,
				141));
		list = new List(group1, SWT.NONE);
		list
				.setBounds(new org.eclipse.swt.graphics.Rectangle(13, 19, 105,
						108));
		button2 = new Button(group1, SWT.NONE);
		button2.setBounds(new org.eclipse.swt.graphics.Rectangle(137, 42, 59,
				22));
		button2.setText("Add >");
		button3 = new Button(group1, SWT.NONE);
		button3.setBounds(new org.eclipse.swt.graphics.Rectangle(137, 79, 59,
				22));
		button3.setText("< Remove");
		createTable1();
	}

	/**
	 * This method initializes table1
	 * 
	 */
	private void createTable1() {
		table1 = new Table(group1, SWT.NONE);
		table1.setHeaderVisible(true);
		table1.setLinesVisible(true);
		table1.setBounds(new org.eclipse.swt.graphics.Rectangle(211, 23, 189,
				104));
	}

	/**
	 * This method initializes group2
	 * 
	 */
	private void createGroup2() {
		group2 = new Group(composite1, SWT.NONE);
		group2.setText("Log file");
		group2.setBounds(new org.eclipse.swt.graphics.Rectangle(12, 6, 412, 43));
		label1 = new Label(group2, SWT.NONE);
		label1.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 19, 38, 12));
		label1.setText("Type :");
		label2 = new Label(group2, SWT.NONE);
		label2.setBounds(new org.eclipse.swt.graphics.Rectangle(48, 18, 72, 15));
		label2.setText("Active log");
		label3 = new Label(group2, SWT.NONE);
		label3.setBounds(new org.eclipse.swt.graphics.Rectangle(127, 19, 32, 14));
		label3.setText("File");
		text = new Text(group2, SWT.BORDER);
		text.setBounds(new org.eclipse.swt.graphics.Rectangle(163, 14, 138, 22));
		text.setEditable(false);
		text.setText(logFileName);
		button6 = new Button(group2, SWT.NONE);
		button6.setBounds(new org.eclipse.swt.graphics.Rectangle(301, 15, 102, 20));
		button6.setText("Read new log file");
	}
}
