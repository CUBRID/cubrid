/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.common.dialog;

import org.eclipse.core.runtime.IProduct;
import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.resource.JFaceColors;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.internal.dialogs.ProductInfoDialog;

import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.Version;

/**
 * 
 * This dialog show about message about CUBRID Manager
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-15 created by pangqiren
 */
@SuppressWarnings("restriction")
public class AboutDialog extends
		ProductInfoDialog {

	private static final String homePage = " http://www.cubrid.com";
	private static final String projectSite = "http://dev.naver.com/projects/cubrid";
	public static final String cmProjectSite = "http://dev.naver.com/projects/cubrid-manager";
	private String productName;
	private IProduct product;
	private StyledText text;

	public AboutDialog(Shell parentShell) {
		super(parentShell);
		product = Platform.getProduct();
		if (product != null)
			productName = product.getName();
		if (productName == null)
			productName = Messages.productName;
		String message = Messages.bind(Messages.aboutMessage, new String[] {
				productName, Version.releaseStr, Version.buildId, homePage,
				projectSite, cmProjectSite });
		this.setItem(scan(message));
	}

	@Override
	protected int getShellStyle() {
		return super.getShellStyle() | SWT.RESIZE | SWT.MAX;
	}

	@Override
	protected void configureShell(Shell newShell) {
		super.configureShell(newShell);
		newShell.setText(Messages.bind(Messages.titleAboutDialog,
				new String[] { Messages.productName }));
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, true);
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		final Cursor hand = new Cursor(parent.getDisplay(), SWT.CURSOR_HAND);
		final Cursor busy = new Cursor(parent.getDisplay(), SWT.CURSOR_WAIT);
		setHandCursor(hand);
		setBusyCursor(busy);
		getShell().addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				setHandCursor(null);
				hand.dispose();
				setBusyCursor(null);
				busy.dispose();
			}
		});

		// brand the about box if there is product info
		Image aboutImage = null;
		if (product != null) {
			ImageDescriptor imageDescriptor = CubridManagerUIPlugin.getImageDescriptor("icons/about.gif");
			if (imageDescriptor != null) {
				aboutImage = imageDescriptor.createImage();
			}
		}

		Composite workArea = new Composite(parent, SWT.NONE);
		GridLayout workLayout = new GridLayout();
		workLayout.marginHeight = 0;
		workLayout.marginWidth = 0;
		workLayout.verticalSpacing = 0;
		workLayout.horizontalSpacing = 0;
		workArea.setLayout(workLayout);
		workArea.setLayoutData(new GridData(GridData.FILL_BOTH));

		// page group
		Color background = JFaceColors.getBannerBackground(parent.getDisplay());
		Color foreground = JFaceColors.getBannerForeground(parent.getDisplay());
		Composite top = (Composite) super.createDialogArea(workArea);

		// override any layout inherited from createDialogArea 
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		layout.verticalSpacing = 0;
		layout.horizontalSpacing = 0;
		top.setLayout(layout);
		top.setLayoutData(new GridData(GridData.FILL_BOTH));
		top.setBackground(background);
		top.setForeground(foreground);

		// the image & text	
		Composite topContainer = new Composite(top, SWT.NONE);
		topContainer.setBackground(background);
		topContainer.setForeground(foreground);

		layout = new GridLayout();
		layout.numColumns = (aboutImage == null || getItem() == null ? 1 : 2);
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		layout.verticalSpacing = 0;
		layout.horizontalSpacing = 0;
		topContainer.setLayout(layout);
		GridData data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		data.grabExcessHorizontalSpace = true;
		topContainer.setLayoutData(data);

		//image on left side of dialog
		if (aboutImage != null) {
			Label imageLabel = new Label(topContainer, SWT.NONE);
			imageLabel.setBackground(background);
			imageLabel.setForeground(foreground);

			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = false;
			imageLabel.setLayoutData(data);
			imageLabel.setImage(aboutImage);
		}

		if (getItem() != null) {
			Composite textContainer = new Composite(topContainer, SWT.NONE);
			textContainer.setBackground(background);
			textContainer.setForeground(foreground);

			layout = new GridLayout();
			layout.numColumns = 1;
			textContainer.setLayout(layout);
			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = true;
			textContainer.setLayoutData(data);

			// text on the right
			text = new StyledText(textContainer, SWT.MULTI | SWT.READ_ONLY);
			text.setCaret(null);
			text.setFont(parent.getFont());
			data = new GridData();
			data.horizontalAlignment = GridData.FILL;
			data.verticalAlignment = GridData.BEGINNING;
			data.grabExcessHorizontalSpace = true;
			text.setText(getItem().getText());
			text.setLayoutData(data);
			text.setCursor(null);
			text.setBackground(background);
			text.setForeground(foreground);
			setLinkRanges(text, getItem().getLinkRanges());
			addListeners(text);
		}

		// horizontal bar
		Label bar = new Label(workArea, SWT.HORIZONTAL | SWT.SEPARATOR);
		data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		bar.setLayoutData(data);

		// add image buttons for bundle groups that have them
		Composite bottom = (Composite) super.createDialogArea(workArea);
		// override any layout inherited from createDialogArea 
		layout = new GridLayout();
		bottom.setLayout(layout);
		bottom.setLayoutData(new GridData(GridData.FILL_BOTH));

		// spacer
		bar = new Label(bottom, SWT.NONE);
		data = new GridData();
		data.horizontalAlignment = GridData.FILL;
		bar.setLayoutData(data);
		return workArea;
	}
}