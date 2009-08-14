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
package com.cubrid.cubridmanager.ui.cubrid.table.control;

import java.util.List;

import org.eclipse.jface.viewers.ILabelProviderListener;
import org.eclipse.jface.viewers.ITableLabelProvider;
import org.eclipse.swt.graphics.Image;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBAttribute;
import com.cubrid.cubridmanager.core.cubrid.table.model.DataType;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;

public class AttributeTableViewerLabelProvider implements
		ITableLabelProvider {
	SchemaInfo schema = null;
	private List<SchemaInfo> supers;
	DatabaseInfo database;

	public AttributeTableViewerLabelProvider(DatabaseInfo database,
			SchemaInfo schema) {
		this.schema = schema;
		this.database = database;
		supers = SuperClassUtil.getSuperClasses(database, schema);
	}

	private Image pkImage = CubridManagerUIPlugin.getImage("icons/primary_key.png");
	private Image checkImage = CubridManagerUIPlugin.getImage("icons/checked.gif");
	private Image inheritPKImage = CubridManagerUIPlugin.getImage("icons/inherit_primary_key.png");

	public Image getColumnImage(Object element, int columnIndex) {
		DBAttribute a = (DBAttribute) element;
		switch (columnIndex) {
		case 0:
			if (!a.isClassAttribute()) {
				String attrName = a.getName();
				if (a.getInherit().equals(schema.getClassname())) {
					Constraint pk = schema.getPK(supers);
					if (null != pk) {
						if (pk.getAttributes().contains(attrName)) {
							return pkImage;
						}
					}
				} else {
					List<Constraint> pkList = schema.getInheritPK(supers);
					for (Constraint inheritPK : pkList) {
						if (inheritPK.getAttributes().contains(attrName)) {
							return inheritPKImage;
						}
					}
				}
			}
			return null;
		case 1:
			return null;
		case 2:
			return null;
		case 3:
			SerialInfo autoIncrement = a.getAutoIncrement();
			if (null != autoIncrement) {
				return checkImage;
			}
			return null;
		case 4:
			return null;
		case 5:
			if (!a.isClassAttribute()) {
				String attrName = a.getName();
				if (a.getInherit().equals(schema.getClassname())) {
					Constraint pk = schema.getPK(supers);
					if (null != pk) {
						if (pk.getAttributes().contains(attrName)) {
							return checkImage;
						}
					}
				} else {
					List<Constraint> pkList = schema.getInheritPK(supers);
					for (Constraint inheritPK : pkList) {
						if (inheritPK.getAttributes().contains(attrName)) {
							return checkImage;
						}
					}
				}
			}
			if (a.isNotNull()) {
				return checkImage;
			} else {
				return null;
			}
		case 6:
			if (!a.isClassAttribute()) {
				String attrName = a.getName();
				if (a.getInherit().equals(schema.getClassname())) {
					Constraint pk = schema.getPK(supers);
					if (null != pk) {
						if (pk.getAttributes().contains(attrName)) {
							return checkImage;
						}
					}
				} else {
					List<Constraint> pkList = schema.getInheritPK(supers);
					for (Constraint inheritPK : pkList) {
						if (inheritPK.getAttributes().contains(attrName)) {
							return checkImage;
						}
					}
				}
			}
			if (a.isUnique() && schema.isAttributeUnique(a, supers)) {

				return checkImage;
			} else {
				return null;
			}
		case 7:
			if (a.isShared()) {
				return checkImage;
			} else {
				return null;
			}
		case 8:
			return null;
		case 9:
			if (a.isClassAttribute()) {
				return checkImage;
			} else {
				return null;
			}
		case 10:
			return null;
		default:
			break;
		}
		return null;
	}

	public String getColumnText(Object element, int columnIndex) {
		DBAttribute a = (DBAttribute) element;

		switch (columnIndex) {
		case 0:
			return null;
		case 1:
			return a.getName();
		case 2:
			return DataType.getShownType(a.getType());
		case 3:
			return null;
		case 4:
			return a.isShared() ? "" : a.getDefault();
		case 5:
			return null;
		case 6:
			return null;
		case 7:
			return null;
		case 8:
			String inherit = a.getInherit();
			if (inherit.equals(schema.getClassname())) {
				return null;
			}
			return inherit;
		case 9:
			if (a.isClassAttribute()) {
				return null;
			} else {
				return null;
			}
		case 10:
			if (a.isClassAttribute()) {
				return Boolean.TRUE.toString();
			} else {
				return Boolean.FALSE.toString();
			}
		default:
			break;
		}
		return null;
	}

	public void addListener(ILabelProviderListener listener) {

	}

	public void dispose() {

	}

	public boolean isLabelProperty(Object element, String property) {
		return false;
	}

	public void removeListener(ILabelProviderListener listener) {
	}

	public SchemaInfo getSchema() {
		return schema;
	}

	public void setSchema(SchemaInfo schema) {
		this.schema = schema;
		supers = SuperClassUtil.getSuperClasses(database, schema);
	}

}
