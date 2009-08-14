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
package com.cubrid.cubridmanager.ui.cubrid.table.dialog;

import java.math.BigInteger;
import java.text.ParseException;
import java.util.List;

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;

import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetTablesTask;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.ValidateUtil;
import com.cubrid.cubridmanager.ui.spi.dialog.CMTitleAreaDialog;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

public class AddAttributeDialog extends
		CMTitleAreaDialog {

	private static final String MIN_VALUE = "minValue";
	private static final String MAX_VALUE = "maxValue";
	private static final String MAX_FLOAT_DIGITNUM = "38";
	private static final String MAXSIZE = "1073741823";
	private static final String NCHARMAXSIZE = "536870911";

	private Composite sShell = null;
	private CubridDatabase database;
	private Composite composite;
	private Button[] attributeTypeRadio = null;
	private Text columnName = null;
	private Text defaultText = null;

	private Combo dataTypeCombo = null;

	private Button uniqueCheck = null;
	private Button notNullCheck = null;

	private Button autoIncrementButton;
	private Text seedSpinner;
	private Text incrementSpinner;

	private Label dataTypeInLabel;
	private Combo dataTypeInSetCombo;
	private Label sizeLBL;
	private Text sizeSpinner;
	private Label precisionLBL;
	private Text precisionSpinner;
	private Composite dataTypeInSetComposite;
	private Composite sizeComposite;
	private Composite precisionComposite;
	private Group fieldDimensionGroup;
	private List<String> tableList;
	private Composite autoIncrementComposite;

	Boolean multiByteSupported = null;
	private Button sharedButton;
	private Label defaultOrSharedLabel;
	private SchemaInfo schema;
	boolean isEditAll;
	private DBAttribute editAttribute;
	private DBAttribute oldAttribute;
	private boolean isOldAttributeClassType;
	private boolean isClassAttribute;
	private List<SchemaInfo> supers;
	private boolean showObjectConcept;
	private boolean hasSetData = false;

	private void refresh() {
		sShell.pack();
		composite.pack();
		getShell().pack();
	}

	private void hideSize() {
		if (null != sizeComposite) {
			sizeComposite.dispose();
			sizeComposite = null;
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void showSize() {
		if (sizeComposite == null) {
			createSizeComposite();
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void hidePrecision() {
		if (null != precisionComposite) {
			precisionComposite.dispose();
			precisionComposite = null;
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void showPrecision() {
		if (precisionComposite == null) {
			createPrecisionComposite();
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void hideDataTypeInSet() {
		if (null != dataTypeInSetComposite) {
			dataTypeInSetComposite.dispose();
			dataTypeInSetComposite = null;
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void showDataTypeInSet() {
		if (dataTypeInSetComposite == null) {
			createDataTypeInSetComposite();
			fieldDimensionGroup.layout(true);
		}
		refresh();
	}

	private void fireAttributeTypeChanged() {

		if (attributeTypeRadio[1].getSelection()) { //class
			//class type attribute can't be shared and unique,
			uniqueCheck.setSelection(false);
			uniqueCheck.setEnabled(false);
			defaultText.setEnabled(true);
			sharedButton.setSelection(false);
			sharedButton.setEnabled(false);

			//if autoincrement exists, hide it.	
			hideAutoIncrement();
		} else { //instance
			uniqueCheck.setEnabled(true);
			uniqueCheck.setSelection(false);
			sharedButton.setEnabled(true);
			sharedButton.setSelection(false);
			//if autoincrement exists, hide it.	
			if (shouldShowAutoIncrement()) {
				showAutoIncrement();
			} else {
				hideAutoIncrement();
			}
		}

	}

	private boolean isMultiBytesSupported() {
		if (null == multiByteSupported) {
			ServerInfo serverInfo = database.getServer().getServerInfo();
			String intl_mbs_support = serverInfo.getCubridConfPara(
					ConfConstants.intl_mbs_support, database.getLabel());
			boolean f = intl_mbs_support != null
					&& intl_mbs_support.equals("yes"); //$NON-NLS-1$
			multiByteSupported = f ? Boolean.TRUE : Boolean.FALSE;
		}
		return multiByteSupported.booleanValue();

	}

	private boolean validateAll() {
		setErrorMessage(null);
		getButton(IDialogConstants.OK_ID).setEnabled(false);
		if (!validateColumnName()) {
			return false;
		}
		if (!validateDataType()) {
			return false;
		}
		if (!validateDefaultOrShared()) {
			return false;
		}
		if (!validateNotNull()) {
			return false;
		}
		if (!validateSize()) {
			return false;
		}
		if (!validatePrecision()) {
			return false;
		}
		if (!validateSeed()) {
			return false;
		}
		if (!validateIncrement()) {
			return false;
		}
		getButton(IDialogConstants.OK_ID).setEnabled(true);
		return true;
	}

	private boolean validateNotNull() {
		String value = defaultText.getText();
		boolean isNotNull = notNullCheck.getSelection();
		value = value.equals("") ? null : value;
		boolean isClassAttr = attributeTypeRadio[0].getSelection() ? false
				: true;
		if (isClassAttr && isNotNull && value == null) {
			setErrorMessage(Messages.errNoDefaultOnClassColumnNotNull);
			return false;
		}
		return true;
	}

	private boolean validateDefaultOrShared() {
		String value = defaultText.getText();
		String dataType = makeType();
		if (!value.equals("")) { //$NON-NLS-1$			
			try {
				checkValue(dataType, value);
			} catch (NumberFormatException e) {
				String msg = Messages.bind(Messages.errParseValue2DataType,
						value, dataType);
				setErrorMessage(msg);
				return false;
			} catch (ParseException pe) {
				if (dataType.equalsIgnoreCase("timestamp")) { //$NON-NLS-1$		
					setErrorMessage(Messages.invalidTimestamp);
				} else if (dataType.equalsIgnoreCase("date")) { //$NON-NLS-1$
					setErrorMessage(Messages.invalidDate);
				} else if (dataType.equalsIgnoreCase("time")) { //$NON-NLS-1$
					setErrorMessage(Messages.invalidTime);
				} else if (dataType.equalsIgnoreCase("datetime")) { //$NON-NLS-1$
					setErrorMessage(Messages.invalidDatetime);
				}
				this.getShell().pack();
				return false;
			}
		}
		return true;
	}

	private boolean validateDataType() {
		String dataType = dataTypeCombo.getText();
		boolean isValidDataType = false;
		for (int i = 1; i < dataTypeCombo.getItemCount(); i++) {
			if (dataTypeCombo.getItem(i).equalsIgnoreCase(dataType)) {
				dataType = dataTypeCombo.getItem(i);
				isValidDataType = true;
				break;
			}
		}
		if (isValidDataType) {
			if (dataType.equals("SET") || dataType.equals("MULTISET") //$NON-NLS-1$ //$NON-NLS-2$
					|| dataType.equals("SEQUENCE")) {
				String subdateType = dataTypeInSetCombo.getText();
				if (subdateType.equals("")) {
					isValidDataType = false;
				}
			}

		}
		if (!isValidDataType) {
			if (dataType.equalsIgnoreCase("User input")) { //$NON-NLS-1$
				setErrorMessage(Messages.errSelectDataType);
				return false;
			} else {
				setErrorMessage(Messages.errInvalidDataType);
				return false;
			}
		}
		return true;
	}

	private boolean validateColumnName() {
		String attname = columnName.getText();
		if (!isMultiBytesSupported() && !CommonTool.isASCII(attname)) {
			setErrorMessage(Messages.errMultiBytes);
			return false;
		}
		String retstr = CommonTool.validateCheckInIdentifier(attname);
		if (retstr.length() > 0) {
			setErrorMessage(Messages.errColumnName);
			return false;
		}

		boolean isClassAttr = attributeTypeRadio[0].getSelection() ? false
				: true;
		boolean judgeDuplicateName = false;
		if (oldAttribute == null) { //new attr
			judgeDuplicateName = true;
		} else if (oldAttribute != null
				&& !oldAttribute.getName().equals(attname)) { //attrName changed
			judgeDuplicateName = true;
		} else if (oldAttribute != null
				&& !oldAttribute.isClassAttribute() == isClassAttr) { //attrType changed
			judgeDuplicateName = true;
		}
		if (judgeDuplicateName) {
			DBAttribute attribute = schema.getDBAttributeByName(attname,
					isClassAttr);
			String className = schema.getClassname() == null ? ""
					: schema.getClassname();
			if (null != attribute) {
				if (className.equals(attribute.getInherit())) {
					setErrorMessage(Messages.bind(Messages.errColumnExist,
							attname));
					return false;
				}
			}
		}
		return true;
	}

	private void addListener() {
		columnName.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				boolean valid = validateColumnName();
				if (valid) {
					validateAll();
				} else {
					getButton(IDialogConstants.OK_ID).setEnabled(false);
				}
			}
		});
		attributeTypeRadio[1].addSelectionListener(new SelectionAdapter() { //class
			public void widgetSelected(SelectionEvent e) {
				fireAttributeTypeChanged();
				validateAll();
			}
		});
		uniqueCheck.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireUniqueChanged();
			}
		});
		notNullCheck.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				validateAll();
			}
		});
		dataTypeCombo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (0 == dataTypeCombo.getSelectionIndex()) {
					hideAutoIncrement();
					hidePrecision();
					hideSize();
				} else {
					String dateType = dataTypeCombo.getText();
					hideDataTypeInSet();
					hideSize();
					hidePrecision();
					fireDataTypeChanged(dateType, true);
				}
				validateAll();
			}
		});
		defaultText.addModifyListener(new ModifyListener() {
			public void modifyText(ModifyEvent e) {
				if (validateDefaultOrShared()) {
					fireDefaultOrSharedValueChanged();
					validateAll();
				} else {
					getButton(IDialogConstants.OK_ID).setEnabled(false);
				}
			}
		});
		sharedButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				fireSharedButtonChanged();
			}
		});
		addAutoIncrementListeners();
		addSizeListener();
		addPrecisionListener();
		addDataTypeInSetListener();
	}

	private void addPrecisionListener() {
		if (precisionComposite != null) {
			precisionSpinner.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validatePrecision();
					if (!valid) {
						getButton(IDialogConstants.OK_ID).setEnabled(false);
					} else {
						validateAll();
					}
					if (shouldShowAutoIncrement()) {
						showAutoIncrement();
					} else {
						hideAutoIncrement();
					}
				}
			});
		}
	}

	private void addSizeListener() {
		if (sizeComposite != null) {
			sizeSpinner.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validateSize();
					if (!valid) {
						getButton(IDialogConstants.OK_ID).setEnabled(false);
					} else {
						if (autoIncrementComposite != null) {

						}
						validateAll();
					}
				}
			});
		}
	}

	private void addAutoIncrementListeners() {

		if (autoIncrementComposite != null) {
			autoIncrementButton.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent e) {
					fireAutoIncrementSelectionChanged();
				}
			});
			seedSpinner.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validateSeed();
					if (!valid) {
						getButton(IDialogConstants.OK_ID).setEnabled(false);
					} else {
						validateAll();
					}
				}
			});
			incrementSpinner.addModifyListener(new ModifyListener() {
				public void modifyText(ModifyEvent e) {
					boolean valid = validateIncrement();
					if (!valid) {
						getButton(IDialogConstants.OK_ID).setEnabled(false);
					} else {
						validateAll();
					}
				}
			});
		}
	}

	private boolean shouldEnableUniqueCHK() {
		boolean shouldEnable = true;
		if (attributeTypeRadio[0].getSelection()) {//instance
			if (sharedButton.getSelection()) {
				return false;
			}
			String dataType = dataTypeCombo.getText();
			if (dataType.equals("SET") || dataType.equals("MULTISET") //$NON-NLS-1$ //$NON-NLS-2$
					|| dataType.equals("SEQUENCE")) { //$NON-NLS-1$
				return false;
			}
		}
		if (attributeTypeRadio[1].getSelection()) { //class
			return false;
		}
		return shouldEnable;
	}

	/**
	 * Constructor for creating a new column
	 * 
	 * @param parent
	 * @param database
	 * @param schema
	 */
	public AddAttributeDialog(Shell parent, CubridDatabase database,
			SchemaInfo schema, boolean showObjectConcept) {
		super(parent);
		this.database = database;
		this.schema = schema;
		supers = SuperClassUtil.getSuperClasses(database.getDatabaseInfo(),
				schema);
		oldAttribute = null;
		editAttribute = new DBAttribute();
		this.isEditAll = true;
		this.showObjectConcept = showObjectConcept;
	}

	/**
	 * Constructor for edit a column
	 * 
	 * @param parent
	 * @param database
	 * @param schema
	 * @param attrName
	 * @param isEditAll
	 * @param isClassAttr
	 */
	public AddAttributeDialog(Shell parent, CubridDatabase database,
			SchemaInfo schema, String attrName, boolean isClassAttr,
			boolean isEditAll, boolean showObjectConcept) {
		super(parent);
		this.database = database;
		this.schema = schema;
		supers = SuperClassUtil.getSuperClasses(database.getDatabaseInfo(),
				schema);
		if (null == attrName) {
			oldAttribute = null;
			editAttribute = new DBAttribute();
		} else {
			isOldAttributeClassType = isClassAttr;
			oldAttribute = schema.getDBAttributeByName(attrName, isClassAttr);
			editAttribute = oldAttribute.clone();
		}
		this.isEditAll = isEditAll;
		this.showObjectConcept = showObjectConcept;
	}

	@Override
	protected void constrainShellSize() {
		super.constrainShellSize();
		CommonTool.centerShell(getShell());
		refresh();
	}

	@Override
	protected void createButtonsForButtonBar(Composite parent) {
		createButton(parent, IDialogConstants.OK_ID, Messages.btnOK, true);
		createButton(parent, IDialogConstants.CANCEL_ID, Messages.btnCancel,
				false);
		if (this.oldAttribute == null) {
			getButton(IDialogConstants.OK_ID).setEnabled(false);
		} else {
			getButton(IDialogConstants.OK_ID).setEnabled(true);
		}
	}

	private String checkValue(String dataType, String dataValue) throws NumberFormatException,
			ParseException {
		if (dataValue.equals("")) { //$NON-NLS-1$
			return ""; //$NON-NLS-1$
		} else {
			if (!dataValue.equals("")) { //$NON-NLS-1$
				dataValue = DBAttribute.formatValue(dataType, dataValue);
				return dataValue;
			}
			return null;
		}
	}

	@Override
	protected void buttonPressed(int buttonId) {
		if (buttonId == IDialogConstants.OK_ID) {
			isClassAttribute = this.attributeTypeRadio[0].getSelection() ? false
					: true;
			DBAttribute retAttribute = new DBAttribute();
			retAttribute.setName(columnName.getText().trim());

			String dataType = makeType();
			retAttribute.setType(DataType.getType(dataType));
			String value = defaultText.getText();
			value = value.equals("") ? null : value;
			if (sharedButton.getSelection()) {
				retAttribute.setShared(true);
				retAttribute.setSharedValue(value);
			} else {
				retAttribute.setShared(false);
				retAttribute.setDefault(value);
			}
			boolean isNotNull = notNullCheck.getSelection();

			if (null != autoIncrementComposite) {
				if (autoIncrementButton.getSelection()) {
					SerialInfo autoIncrement = new SerialInfo();
					autoIncrement.setMinValue(seedSpinner.getText());
					autoIncrement.setIncrementValue(incrementSpinner.getText());
					retAttribute.setAutoIncrement(autoIncrement);
				}
			}

			retAttribute.setNotNull(isNotNull);
			retAttribute.setUnique(uniqueCheck.getSelection());
			retAttribute.setInherit(schema.getClassname()); //$NON-NLS-1$
			retAttribute.setClassAttribute(isClassAttribute);
			editAttribute = retAttribute;

			//check data type compatible at last for performance
			List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
					database.getDatabaseInfo(), schema);
			SchemaInfo newSchema = schema.clone();
			boolean isNewAttrClass = editAttribute.isClassAttribute();
			if (oldAttribute != null) {
				boolean isOldAttrClass = oldAttribute.isClassAttribute();
				if (isOldAttrClass != isNewAttrClass) { // attribute
					// type
					// changed
					newSchema.removeDBAttributeByName(oldAttribute.getName(),
							isOldAttrClass);
					newSchema.addDBAttribute(editAttribute, isNewAttrClass);
				} else {
					newSchema.replaceDBAttributeByName(oldAttribute,
							editAttribute, isNewAttrClass, superList);
				}
			} else {
				newSchema.removeDBAttributeByName(editAttribute.getName(),
						isNewAttrClass);
				newSchema.addDBAttribute(editAttribute, isNewAttrClass);
			}
			boolean success = SuperClassUtil.fireSuperClassChanged(
					database.getDatabaseInfo(), schema, newSchema,
					schema.getSuperClasses());
			if (!success) {
				CommonTool.openErrorBox(Messages.errDataTypeImcompatible);
				return;
			}
			getShell().dispose();
			close();
		} else if (buttonId == IDialogConstants.CANCEL_ID) {
			super.buttonPressed(buttonId);
			getShell().dispose();
			close();
		}
	}

	@Override
	protected Control createDialogArea(Composite parent) {
		Composite parentComp = (Composite) super.createDialogArea(parent);
		getHelpSystem().setHelp(parentComp,
				CubridManagerHelpContextIDs.databaseTable);
		composite = new Composite(parentComp, SWT.NONE);
		final GridData gd_composite = new GridData(SWT.FILL, SWT.FILL, true,
				true);
		gd_composite.widthHint = 550;
		composite.setLayoutData(gd_composite);

		GridLayout layout = new GridLayout();
		layout.numColumns = 1;
		layout.marginHeight = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_MARGIN);
		layout.marginWidth = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_MARGIN);
		layout.verticalSpacing = convertVerticalDLUsToPixels(IDialogConstants.VERTICAL_SPACING);
		layout.horizontalSpacing = convertHorizontalDLUsToPixels(IDialogConstants.HORIZONTAL_SPACING);
		composite.setLayout(layout);
		createComposite();
		setData();
		hasSetData = true;
		addListener();
		return parentComp;
	}

	private void setData() {
		if (this.oldAttribute == null) {
			attributeTypeRadio[0].setSelection(true);
			dataTypeCombo.select(0);
			setTitle(Messages.msgTitleAddColumn);
			setMessage(Messages.msgAddColumn);
			getShell().setText(Messages.addAttrShellTitle);

		} else {
			/**
			 * set information
			 */
			// set column type
			if (!isOldAttributeClassType) {
				attributeTypeRadio[0].setSelection(true);
			} else {
				attributeTypeRadio[1].setSelection(true);
			}
			// below method would set shared button
			fireAttributeTypeChanged();

			// set column name
			columnName.setText(editAttribute.getName());

			//set date type
			String dataType = DataType.getShownType(editAttribute.getType());
			setDataType(dataType);

			//set shared,shared value or default value
			String defaultOrSharedValue = null;
			if (editAttribute.isShared()) {
				sharedButton.setSelection(true);
				defaultOrSharedValue = editAttribute.getSharedValue();
			} else {
				sharedButton.setSelection(false);
				defaultOrSharedValue = editAttribute.getDefault();
			}
			fireSharedButtonChanged();
			if (null != defaultOrSharedValue) {
				defaultText.setText(defaultOrSharedValue);
				fireDefaultOrSharedValueChanged();
			} else {
				SerialInfo autoIncrement = editAttribute.getAutoIncrement();
				if (autoIncrement != null) {
					autoIncrementButton.setSelection(true);
					seedSpinner.setText(autoIncrement.getMinValue());
					incrementSpinner.setText(autoIncrement.getIncrementValue());
					fireAutoIncrementSelectionChanged();
				}
			}
			//set unique
			if (editAttribute.isUnique()
					&& schema.isAttributeUnique(editAttribute, supers)) {
				uniqueCheck.setSelection(true);
				fireUniqueChanged();
			}

			//set not null
			if (editAttribute.isNotNull()) {
				notNullCheck.setSelection(true);
			} else {
				notNullCheck.setSelection(false);
			}

			if (isEditAll) {
				// Column type
				attributeTypeRadio[0].setEnabled(true);
				attributeTypeRadio[1].setEnabled(true);
				// auto increment				
				if (autoIncrementComposite != null) {
					autoIncrementButton.setEnabled(true);
					seedSpinner.setEnabled(true);
					incrementSpinner.setEnabled(true);
				}
				notNullCheck.setEnabled(true);
				if (!oldAttribute.isClassAttribute()) {
					String attrName = oldAttribute.getName();
					List<SchemaInfo> supers = SuperClassUtil.getSuperClasses(
							database.getDatabaseInfo(), schema);
					Constraint pk = schema.getPK(supers);
					if (null != pk) {
						if (pk.getAttributes().contains(attrName)) {
							notNullCheck.setEnabled(false);
						}
					}
				}
			} else {
				// Column type
				attributeTypeRadio[0].setEnabled(false);
				attributeTypeRadio[1].setEnabled(false);
				sharedButton.setEnabled(false);
				if (sharedButton.getSelection()) {
					defaultText.setEnabled(false);
				}

				// Unique and Not Null
				uniqueCheck.setEnabled(false);
				if (!editAttribute.isClassAttribute()
						&& !editAttribute.isShared()) {
					String attrName = editAttribute.getName();
					List<SchemaInfo> supers = SuperClassUtil.getSuperClasses(
							database.getDatabaseInfo(), schema);
					Constraint pk = schema.getPK(supers);
					if (null != pk) {
						if (pk.getAttributes().contains(attrName)) {
							notNullCheck.setEnabled(false);
						} else {
							notNullCheck.setEnabled(true);
						}
					} else {
						notNullCheck.setEnabled(true);
					}
				} else {
					notNullCheck.setEnabled(false);
				}

				// auto increment
				if (autoIncrementComposite != null) {
					autoIncrementButton.setEnabled(false);
					seedSpinner.setEnabled(false);
					incrementSpinner.setEnabled(false);
				} else {
					hideAutoIncrement();
				}
			}
			setTitle(Messages.msgTitleEditColumn);
			setMessage(Messages.msgEditColumn);
			getShell().setText(Messages.editAttrShellTitle);
		}

	}

	private void setDataType(String dataType) {
		String outerType = DataType.getTypePart(dataType);
		String typeRemain = DataType.getTypeRemain(dataType);
		setComboSelect(dataTypeCombo, outerType, true);

		if (typeRemain != null) {
			if (outerType.equals("SET") || outerType.equals("MULTISET") //$NON-NLS-1$ //$NON-NLS-2$
					|| outerType.equals("SEQUENCE")) { //$NON-NLS-1$
				String type = DataType.getTypePart(typeRemain);
				setComboSelect(dataTypeInSetCombo, type, false);
				typeRemain = DataType.getTypeRemain(typeRemain);
			}
		}
		if (typeRemain != null) {
			int index = typeRemain.indexOf(","); //$NON-NLS-1$
			if (index != -1) {
				String[] strs = typeRemain.split(","); //$NON-NLS-1$
				String size = strs[0];
				String p = strs[1];
				sizeSpinner.setText(size);
				sizeSpinner.setEnabled(isEditAll || false);
				precisionSpinner.setText(p);
				precisionSpinner.setEnabled(isEditAll || false);
			} else {
				String size = typeRemain;
				sizeSpinner.setText(size);
				sizeSpinner.setEnabled(isEditAll || false);
			}
		}
	}

	private void setComboSelect(Combo dataTypeCombo, String typeText,
			boolean outer) {
		int index = getComboIndex(dataTypeCombo, typeText);
		if (index != -1) {
			dataTypeCombo.select(index);
			dataTypeCombo.setEnabled(isEditAll);
			fireDataTypeChanged(typeText, outer);
		} else {
			setErrorMessage(Messages.errDataTypeNotFound + typeText);
		}
	}

	private int getComboIndex(Combo dataTypeCombo, String text) {
		for (int i = 0; i < dataTypeCombo.getItemCount(); i++) {
			if (dataTypeCombo.getItem(i).equals(text)) {
				return i;
			}
		}
		return -1;
	}

	private void fireSharedButtonChanged() {
		if (sharedButton.getSelection()) {
			defaultOrSharedLabel.setText(Messages.lblSharedValue);
		} else {
			defaultOrSharedLabel.setText(Messages.lblDefaultValue);
		}
		boolean enableUnique = shouldEnableUniqueCHK();
		uniqueCheck.setEnabled(enableUnique);
		if (shouldShowAutoIncrement()) {
			showAutoIncrement();
		} else {
			hideAutoIncrement();
		}
	}

	private void createComposite() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		gridLayout.horizontalSpacing = 10;
		gridLayout.marginWidth = 10;
		gridLayout.verticalSpacing = 10;
		gridLayout.marginHeight = 10;
		sShell = new Composite(composite, SWT.NONE);
		final GridData gd_sShell = new GridData(SWT.FILL, SWT.FILL, true, true);
		sShell.setLayoutData(gd_sShell);
		sShell.setLayout(gridLayout);

		Label label3 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label3.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false, false));
		label3.setText(Messages.lblColumnName);
		columnName = new Text(sShell, SWT.BORDER);
		final GridData gd_columnName = new GridData(SWT.FILL, SWT.CENTER, true,
				false);
		gd_columnName.widthHint = 343;
		columnName.setLayoutData(gd_columnName);
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.marginHeight = 0;
		gridLayout2.verticalSpacing = 0;
		gridLayout2.marginWidth = 0;

		final Composite columnTypeComposite = new Composite(sShell, SWT.NONE);
		final GridData gd_columnTypeComposite = new GridData(SWT.FILL,
				SWT.CENTER, false, false, 2, 1);
		columnTypeComposite.setLayoutData(gd_columnTypeComposite);
		final GridLayout gridLayout_3 = new GridLayout();
		gridLayout_3.marginHeight = 0;
		gridLayout_3.marginWidth = 0;
		columnTypeComposite.setLayout(gridLayout_3);
		final Group columnTypeGroup = new Group(columnTypeComposite, SWT.NONE);
		columnTypeGroup.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		columnTypeGroup.setText(Messages.lblColumnType);
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.makeColumnsEqualWidth = true;
		gridLayout_1.numColumns = 3;
		columnTypeGroup.setLayout(gridLayout_1);
		attributeTypeRadio = new Button[3];
		attributeTypeRadio[0] = new Button(columnTypeGroup, SWT.RADIO);
		attributeTypeRadio[0].setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false));
		attributeTypeRadio[0].setText(Messages.typeInstance);

		attributeTypeRadio[1] = new Button(columnTypeGroup, SWT.RADIO);
		attributeTypeRadio[1].setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false));
		attributeTypeRadio[1].setText(Messages.typeClass);

		sharedButton = new Button(columnTypeGroup, SWT.CHECK);
		sharedButton.setText(Messages.typeShared);

		if (showObjectConcept) {
			gd_columnTypeComposite.heightHint = -1;
		} else {
			gd_columnTypeComposite.heightHint = 0;
		}

		fieldDimensionGroup = new Group(sShell, SWT.NONE);
		gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 0;
		gridLayout.numColumns = 2;
		fieldDimensionGroup.setLayout(gridLayout);
		fieldDimensionGroup.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1));
		fieldDimensionGroup.setText(Messages.grpFieldDesc);
		Label label4 = new Label(fieldDimensionGroup, SWT.LEFT | SWT.WRAP);
		label4.setLayoutData(new GridData(140, SWT.DEFAULT));
		label4.setText(Messages.lblDataType);

		dataTypeCombo = new Combo(fieldDimensionGroup, SWT.BORDER
				| SWT.READ_ONLY);
		final GridData gd_dataTypeCombo = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		dataTypeCombo.setLayoutData(gd_dataTypeCombo);
		dataTypeCombo.setVisibleItemCount(20);

		//		createAutoIncrement();

		defaultOrSharedLabel = new Label(sShell, SWT.LEFT | SWT.WRAP);
		defaultOrSharedLabel.setLayoutData(new GridData(120, SWT.DEFAULT));
		defaultOrSharedLabel.setText(Messages.lblDefaultValue);
		defaultText = new Text(sShell, SWT.BORDER);
		defaultText.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, false,
				false));

		final Group fieldFlagGroup = new Group(sShell, SWT.NONE);
		fieldFlagGroup.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 2, 1));
		fieldFlagGroup.setText(Messages.grpConstaint);
		final GridLayout gridLayout_2 = new GridLayout();
		gridLayout_2.numColumns = 2;
		fieldFlagGroup.setLayout(gridLayout_2);
		uniqueCheck = new Button(fieldFlagGroup, SWT.CHECK);
		uniqueCheck.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		uniqueCheck.setText(Messages.typeUnique);

		notNullCheck = new Button(fieldFlagGroup, SWT.CHECK);
		notNullCheck.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		notNullCheck.setText(Messages.typeNotNull);

		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 100;
		gridData31.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.END;

		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 100;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.END;

		addAllDataTypes();

	}

	private void addAllDataTypes() {
		int i = 0;
		dataTypeCombo.add("User input", i++);
		String[][] typeMapping = DataType.getTypeMapping();
		for (String[] map : typeMapping) {
			dataTypeCombo.add(map[0], i++);
		}
		if (!showObjectConcept) {
			dataTypeCombo.remove("OBJECT");
		} else {
			List<String> tList = getTableList();
			for (String table : tList) {
				dataTypeCombo.add(table, i++);
			}
		}
	}

	private List<String> getTableList() {
		if (null == tableList) {
			CubridDatabase db = database;
			DatabaseInfo dbInfo = db.getDatabaseInfo();
			GetTablesTask task = new GetTablesTask(dbInfo);
			tableList = task.getUserTables();
		}
		return tableList;
	}

	private void addNotSetDataTypes() {
		String[][] typeMapping = DataType.getTypeMapping();
		for (int j = 0; j < typeMapping.length; j++) {
			dataTypeInSetCombo.add(typeMapping[j][0]);
		}
		dataTypeInSetCombo.remove("SET");
		dataTypeInSetCombo.remove("MULTISET");
		dataTypeInSetCombo.remove("SEQUENCE");
		if (!showObjectConcept) {
			dataTypeInSetCombo.remove("OBJECT");
		} else {
			List<String> tList = getTableList();
			for (String table : tList) {
				dataTypeInSetCombo.add(table);
			}
		}
	}

	private void fireDataTypeChanged(String dateType, boolean outer) {

		if (dateType.equals("CHAR") || dateType.equals("BIT")) { //$NON-NLS-1$ //$NON-NLS-2$
			hidePrecision();
			showSize();
			sizeSpinner.setData(MAX_VALUE, MAXSIZE);
			sizeSpinner.setText("1");
		} else if (dateType.equals("VARCHAR") || dateType.equals("BIT VARYING")) { //$NON-NLS-1$ //$NON-NLS-2$
			hidePrecision();
			showSize();
			sizeSpinner.setData(MAX_VALUE, MAXSIZE);
			sizeSpinner.setText(MAXSIZE);
		} else if (dateType.equals("NCHAR")) { //$NON-NLS-1$
			hidePrecision();
			showSize();
			sizeSpinner.setData(MAX_VALUE, NCHARMAXSIZE);
			sizeSpinner.setText("1");
		} else if (dateType.equals("NCHAR VARYING")) { //$NON-NLS-1$
			hidePrecision();
			showSize();
			sizeSpinner.setData(MAX_VALUE, NCHARMAXSIZE);
			sizeSpinner.setText(NCHARMAXSIZE);

		} else if (dateType.equals("NUMERIC")) { //$NON-NLS-1$
			showSize();
			showPrecision();
			sizeSpinner.setData(MAX_VALUE, MAX_FLOAT_DIGITNUM);
			sizeSpinner.setText("15");

			precisionSpinner.setData(MAX_VALUE, MAX_FLOAT_DIGITNUM);
			precisionSpinner.setText("0");

		} else if (dateType.equals("SET") || dateType.equals("MULTISET") //$NON-NLS-1$ //$NON-NLS-2$
				|| dateType.equals("SEQUENCE")) { //$NON-NLS-1$
			hideSize();
			hidePrecision();
			showDataTypeInSet();
		} else {
			if (outer) {
				hideDataTypeInSet();
			}
			hideSize();
			hidePrecision();
		}
		hideAutoIncrement(); //init environment		
		if (shouldShowAutoIncrement()) {
			showAutoIncrement();
		} else {
			hideAutoIncrement();
		}
		if (!shouldEnableUniqueCHK()) {
			uniqueCheck.setEnabled(false);
		} else {
			uniqueCheck.setEnabled(true);
		}
	}

	private void createDataTypeInSetComposite() {
		dataTypeInSetComposite = new Composite(fieldDimensionGroup, SWT.NONE);
		dataTypeInSetComposite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1));
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.verticalSpacing = 0;
		gridLayout_1.horizontalSpacing = 0;
		gridLayout_1.marginHeight = 0;
		gridLayout_1.marginWidth = 0;
		gridLayout_1.numColumns = 2;
		dataTypeInSetComposite.setLayout(gridLayout_1);

		dataTypeInLabel = new Label(dataTypeInSetComposite, SWT.NONE);
		dataTypeInLabel.setLayoutData(new GridData(140, SWT.DEFAULT));
		String label = Messages.bind(Messages.dataTypeInSet,
				dataTypeCombo.getText());
		dataTypeInLabel.setText(label);

		dataTypeInSetCombo = new Combo(dataTypeInSetComposite, SWT.NONE
				| SWT.READ_ONLY);
		dataTypeInSetCombo.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false));
		dataTypeInSetCombo.setVisibleItemCount(20);
		addNotSetDataTypes();
		if (hasSetData) {
			addDataTypeInSetListener();
		}
	}

	private void addDataTypeInSetListener() {
		if (dataTypeInSetComposite != null) {
			dataTypeInSetCombo.addSelectionListener(new SelectionAdapter() {
				public void widgetSelected(SelectionEvent e) {
					String dateType = dataTypeInSetCombo.getText();
					hideSize();
					hidePrecision();
					fireDataTypeChanged(dateType, false);
					validateAll();
				}
			});
		}
	}

	private boolean shouldShowAutoIncrement() {
		boolean show = false;
		if (attributeTypeRadio[0].getSelection()) {
			if (sharedButton.getSelection()) {
				return false;
			}
			String value = defaultText.getText();
			if (!value.equals("")) {
				return false;
			}
			String dateType = dataTypeCombo.getText();
			if (dateType.equals("SMALLINT") || dateType.equals("INTEGER") || dateType.equals("BIGINT")) { //$NON-NLS-1$ //$NON-NLS-2$
				show = true;
			} else if (dateType.equals("NUMERIC")) { //$NON-NLS-1$
				if (null != sizeComposite && null != precisionComposite) {
					int size = Integer.parseInt(sizeSpinner.getText());
					int precision = Integer.parseInt(precisionSpinner.getText());
					if (size > 0 & precision == 0) {
						show = true;
					}
				}
			}
		}
		return show;
	}

	private void hideAutoIncrement() {
		if (null != autoIncrementComposite) {
			autoIncrementButton.setSelection(false);
			fireAutoIncrementSelectionChanged();			
			autoIncrementComposite.dispose();
			autoIncrementComposite = null;
			fieldDimensionGroup.layout(true);
			refresh();
		}
	}

	private void showAutoIncrement() {
		if (autoIncrementComposite == null) {
			createAutoIncrement();
			fieldDimensionGroup.layout(true);
			refresh();
		}
	}

	private void createAutoIncrement() {

		autoIncrementComposite = new Composite(fieldDimensionGroup, SWT.NONE);
		autoIncrementComposite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1));
		final GridLayout gridLayout_4 = new GridLayout();
		gridLayout_4.verticalSpacing = 0;
		gridLayout_4.horizontalSpacing = 0;
		gridLayout_4.marginHeight = 0;
		gridLayout_4.marginWidth = 0;
		gridLayout_4.numColumns = 5;
		autoIncrementComposite.setLayout(gridLayout_4);

		autoIncrementButton = new Button(autoIncrementComposite, SWT.CHECK);
		final GridData gd_autoIncrementButton = new GridData(SWT.RIGHT,
				SWT.CENTER, false, false);
		gd_autoIncrementButton.horizontalIndent = 40;
		autoIncrementButton.setLayoutData(gd_autoIncrementButton);
		autoIncrementButton.setText(Messages.btnAutoIncr);

		final Label seedLabel = new Label(autoIncrementComposite, SWT.NONE);
		final GridData gd_seedLabel = new GridData();
		gd_seedLabel.horizontalIndent = 20;
		seedLabel.setLayoutData(gd_seedLabel);
		seedLabel.setText(Messages.lblSeed);

		seedSpinner = new Text(autoIncrementComposite, SWT.BORDER);
		final GridData gd_seedSpinner = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		gd_seedSpinner.widthHint = 50;
		gd_seedSpinner.horizontalIndent = 10;
		seedSpinner.setLayoutData(gd_seedSpinner);
		seedSpinner.setText("1");

		final Label incrementLabel = new Label(autoIncrementComposite, SWT.NONE);
		final GridData gd_incrementLabel = new GridData();
		gd_incrementLabel.horizontalIndent = 20;
		incrementLabel.setLayoutData(gd_incrementLabel);
		incrementLabel.setText(Messages.lblIncr);

		incrementSpinner = new Text(autoIncrementComposite, SWT.BORDER);
		final GridData gd_incrementSpinner = new GridData(SWT.FILL, SWT.CENTER,
				true, false);
		gd_incrementSpinner.widthHint = 50;
		gd_incrementSpinner.horizontalIndent = 10;
		incrementSpinner.setLayoutData(gd_incrementSpinner);
		incrementSpinner.setText("1");

		//		init environment
		seedSpinner.setEnabled(false);
		incrementSpinner.setEnabled(false);

		if (hasSetData) {
			addAutoIncrementListeners();
		}

	}

	private void createSizeComposite() {
		sizeComposite = new Composite(fieldDimensionGroup, SWT.NONE);
		sizeComposite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false, 2, 1));
		final GridLayout gridLayout_2 = new GridLayout();
		gridLayout_2.verticalSpacing = 0;
		gridLayout_2.marginWidth = 0;
		gridLayout_2.horizontalSpacing = 0;
		gridLayout_2.marginHeight = 0;
		gridLayout_2.numColumns = 2;
		sizeComposite.setLayout(gridLayout_2);
		sizeLBL = new Label(sizeComposite, SWT.NONE);
		final GridData gd_sizeLBL = new GridData(SWT.FILL, SWT.CENTER, false,
				false);
		gd_sizeLBL.widthHint = 140;
		sizeLBL.setLayoutData(gd_sizeLBL);
		sizeLBL.setText(Messages.lblSize);

		sizeSpinner = new Text(sizeComposite, SWT.BORDER);
		sizeSpinner.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		sizeSpinner.setData(MIN_VALUE, "1");
		if (hasSetData) {
			addSizeListener();
		}
	}

	private void createPrecisionComposite() {
		precisionComposite = new Composite(fieldDimensionGroup, SWT.NONE);
		precisionComposite.setLayoutData(new GridData(SWT.FILL, SWT.CENTER,
				true, false, 2, 1));
		final GridLayout gridLayout_3 = new GridLayout();
		gridLayout_3.verticalSpacing = 0;
		gridLayout_3.marginHeight = 0;
		gridLayout_3.marginWidth = 0;
		gridLayout_3.horizontalSpacing = 0;
		gridLayout_3.numColumns = 2;
		precisionComposite.setLayout(gridLayout_3);

		precisionLBL = new Label(precisionComposite, SWT.NONE);
		final GridData gd_precisionLBL = new GridData(SWT.FILL, SWT.CENTER,
				false, false);
		gd_precisionLBL.widthHint = 140;
		precisionLBL.setLayoutData(gd_precisionLBL);
		precisionLBL.setText(Messages.lblPrecision);

		precisionSpinner = new Text(precisionComposite, SWT.BORDER);
		precisionSpinner.setLayoutData(new GridData(SWT.FILL, SWT.CENTER, true,
				false));
		precisionSpinner.setData(MIN_VALUE, "0");
		if (hasSetData) {
			addPrecisionListener();
		}

	}

	private boolean validateSize() {
		if (sizeComposite == null) {
			return true;
		}
		String numberStr = sizeSpinner.getText();
		boolean validFormat = ValidateUtil.isInteger(numberStr);
		if (!validFormat) {
			setErrorMessage(Messages.bind(Messages.errNumber, numberStr));
			return false;
		}
		String max = (String) sizeSpinner.getData(MAX_VALUE);
		String min = (String) sizeSpinner.getData(MIN_VALUE);

		BigInteger value = new BigInteger(numberStr);
		BigInteger minValue = new BigInteger(min);
		BigInteger maxValue = new BigInteger(max);
		if (value.compareTo(maxValue) > 0 || value.compareTo(minValue) < 0) {
			String[] strs = new String[] { numberStr, minValue + "",
					maxValue + "" };
			setErrorMessage(Messages.bind(Messages.errRange, strs));
			return false;
		}
		return true;
	}

	private boolean validatePrecision() {
		if (precisionComposite == null) {
			return true;
		}
		String numberStr = precisionSpinner.getText();
		boolean validFormat = ValidateUtil.isInteger(numberStr);
		if (!validFormat) {
			setErrorMessage(Messages.bind(Messages.errNumber, numberStr));
			return false;
		}
		String max = (String) precisionSpinner.getData(MAX_VALUE);
		String min = (String) precisionSpinner.getData(MIN_VALUE);

		BigInteger value = new BigInteger(numberStr);
		BigInteger minValue = new BigInteger(min);
		BigInteger maxValue = new BigInteger(max);
		if (value.compareTo(maxValue) > 0 || value.compareTo(minValue) < 0) {
			String[] strs = new String[] { numberStr, minValue + "",
					maxValue + "" };
			setErrorMessage(Messages.bind(Messages.errRange, strs));
			return false;
		}
		BigInteger size = new BigInteger(sizeSpinner.getText());
		if (value.compareTo(size) > 0) {
			setErrorMessage(Messages.errPrecisionGreaterSize);
			precisionSpinner.setFocus();
			return false;
		}
		return true;

	}

	private String interMakeType(String dataType) {
		// 0 : direct input, 1 ~ 6 : need size, 7 : need precision, scale, 17 ~
		// 19 : set type.
		if (dataType.equals("CHAR") || dataType.equals("BIT") //$NON-NLS-1$ //$NON-NLS-2$
				|| dataType.equals("VARCHAR") || dataType.equals("BIT VARYING") //$NON-NLS-1$ //$NON-NLS-2$
				|| dataType.equals("NCHAR") || dataType.equals("NCHAR VARYING")) { //$NON-NLS-1$ //$NON-NLS-2$
			//			hidePrecision();
			//			showSize();
			return dataType + "(" + sizeSpinner.getText() + ")"; //$NON-NLS-1$ //$NON-NLS-2$

		} else if (dataType.equals("NUMERIC")) { //$NON-NLS-1$
			//			showSize();
			//			showPrecision();
			return dataType + "(" + sizeSpinner.getText() + "," //$NON-NLS-1$ //$NON-NLS-2$
					+ precisionSpinner.getText() + ")"; //$NON-NLS-1$
		} else if (dataType.equals("SET") || dataType.equals("MULTISET") //$NON-NLS-1$ //$NON-NLS-2$
				|| dataType.equals("SEQUENCE")) { //$NON-NLS-1$
			String subdateType = dataTypeInSetCombo.getText();
			return dataType + "(" + interMakeType(subdateType) + ")"; //$NON-NLS-1$ //$NON-NLS-2$
		} else {
			return dataType;
		}
	}

	private String makeType() {
		String dateType = dataTypeCombo.getText();
		return interMakeType(dateType);
	}

	public DBAttribute getEditAttribute() {
		return editAttribute;
	}

	public void setEditAttribute(DBAttribute editAttribute) {
		this.editAttribute = editAttribute;
	}

	public DBAttribute getOldAttribute() {
		return oldAttribute;
	}

	public void setOldAttribute(DBAttribute oldAttribute) {
		this.oldAttribute = oldAttribute;
	}

	public boolean isClassAttribute() {
		return isClassAttribute;
	}

	public void setClassAttribute(boolean isClassAttribute) {
		this.isClassAttribute = isClassAttribute;
	}

	/**
	 * change shared button and default text's statuses on unique button changed
	 * 
	 */
	private void fireUniqueChanged() {
		assert (attributeTypeRadio[0].getSelection() == true && attributeTypeRadio[2].getSelection() == false);
		//instance
		if (uniqueCheck.getSelection()) {
			if (isEditAll || this.oldAttribute == null) {
				sharedButton.setSelection(false);
				sharedButton.setEnabled(false);
			}
		} else {
			if (isEditAll || this.oldAttribute == null) {
				sharedButton.setEnabled(true);
				sharedButton.setSelection(false);
			}
		}
	}

	/**
	 * 
	 * 
	 * @param numberStr
	 */
	private boolean validateSeed() {
		if (autoIncrementComposite == null || autoIncrementComposite != null
				&& seedSpinner.getEnabled() == false) {
			return true;
		}
		String numberStr = seedSpinner.getText();
		boolean validFormat = ValidateUtil.isInteger(numberStr);
		if (!validFormat) {
			setErrorMessage(Messages.bind(Messages.errNumber, numberStr));
			return false;
		}

		String dateType = dataTypeCombo.getText();
		BigInteger minValue = null;
		BigInteger maxValue = null;
		if (dateType.equals("SMALLINT")) {
			minValue = new BigInteger(Short.MIN_VALUE + "");
			maxValue = new BigInteger(Short.MAX_VALUE + "");
		} else if (dateType.equals("INTEGER")) {
			minValue = new BigInteger(Integer.MIN_VALUE + "");
			maxValue = new BigInteger(Integer.MAX_VALUE + "");
		} else if (dateType.equals("NUMERIC")) {
			int digitalNum = Integer.parseInt(sizeSpinner.getText());
			minValue = new BigInteger(DataType.getNumericMinValue(digitalNum));
			maxValue = new BigInteger(DataType.getNumericMaxValue(digitalNum));
		} else if (dateType.equals("BIGINT")) {
			minValue = new BigInteger(Long.MIN_VALUE + "");
			maxValue = new BigInteger(Long.MAX_VALUE + "");
		}

		BigInteger value = new BigInteger(numberStr);

		if (value.compareTo(maxValue) > 0 || value.compareTo(minValue) < 0) {
			String[] strs = new String[] { numberStr, minValue + "",
					maxValue + "" };
			setErrorMessage(Messages.bind(Messages.errRange, strs));
			return false;
		}
		return true;
	}

	/**
	 * 
	 * 
	 * @param numberStr
	 */
	private boolean validateIncrement() {
		if (autoIncrementComposite == null || autoIncrementComposite != null
				&& incrementSpinner.getEnabled() == false) {
			return true;
		}
		String numberStr = incrementSpinner.getText();
		boolean validFormat = ValidateUtil.isInteger(numberStr);
		if (!validFormat) {
			setErrorMessage(Messages.bind(Messages.errNumber, numberStr));
			return false;
		}

		String dateType = dataTypeCombo.getText();
		BigInteger minValue = new BigInteger("1");
		BigInteger maxValue = null;
		if (dateType.equals("SMALLINT")) {
			maxValue = new BigInteger(Short.MAX_VALUE + "");
		} else if (dateType.equals("INTEGER")) {
			maxValue = new BigInteger(Integer.MAX_VALUE + "");
		} else if (dateType.equals("NUMERIC")) {
			int digitalNum = Integer.parseInt(sizeSpinner.getText());
			maxValue = new BigInteger(DataType.getNumericMaxValue(digitalNum));
		} else if (dateType.equals("BIGINT")) {
			maxValue = new BigInteger(Long.MAX_VALUE + "");
		}

		BigInteger value = new BigInteger(numberStr);
		if (value.compareTo(new BigInteger("0")) <= 0) {
			setErrorMessage(Messages.bind(Messages.errIncrement, numberStr));
			return false;
		}
		if (value.compareTo(maxValue) > 0 || value.compareTo(minValue) < 0) {
			String[] strs = new String[] { numberStr, minValue + "",
					maxValue + "" };
			setErrorMessage(Messages.bind(Messages.errRange, strs));
			return false;
		}

		return true;
	}

	private void fireDefaultOrSharedValueChanged() {
		if (isEditAll) {
			if (shouldShowAutoIncrement()) {
				showAutoIncrement();
			} else {
				hideAutoIncrement();
			}
		} else {
			if (!shouldShowAutoIncrement()) {
				hideAutoIncrement();
			}
		}
	}

	private void fireAutoIncrementSelectionChanged() {
		if (autoIncrementButton.getSelection()) {
			seedSpinner.setEnabled(true);
			incrementSpinner.setEnabled(true);
			//disable default
			defaultText.setText("");
			defaultText.setEnabled(false);
		} else {
			seedSpinner.setEnabled(false);
			incrementSpinner.setEnabled(false);
			defaultText.setEnabled(true);
		}
	}
}
