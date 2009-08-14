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
import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClassUtil;

public class FKTableViewerLabelProvider implements
		ITableLabelProvider {
	SchemaInfo schema = null;
	DatabaseInfo database = null;


	public FKTableViewerLabelProvider(SchemaInfo schema, DatabaseInfo database) {
		this.schema = schema;
		this.database = database;
	}

	public Image getColumnImage(Object element, int columnIndex) {

		switch (columnIndex) {
		case 0:
			return null;
		case 1:
			return null;
		case 2:
			return null;
		case 3:

			return null;
		case 4:
			return null;
		case 5:
			return null;
		case 6:
			return null;
		default:
			break;
		}
		return null;
	}

	public String getColumnText(Object element, int columnIndex) {
		Constraint fk = (Constraint) element;

		String refTable = null;
		String delRule = null;
		String updateRule = null;
		String cacheRule = null;
		List<String> rules = fk.getRules();
		for (String rule : rules) {
			String REF_STR = "REFERENCES ";
			String DEL_STR = "ON DELETE ";
			String UPD_STR = "ON UPDATE ";
			String CACHE_STR = "ON CACHE OBJECT ";

			if (rule.startsWith(REF_STR)) {
				refTable = rule.replace(REF_STR, "");
			} else if (rule.startsWith(DEL_STR)) {
				delRule = rule.replace(DEL_STR, "");
			} else if (rule.startsWith(UPD_STR)) {
				updateRule = rule.replace(UPD_STR, "");
			} else if (rule.startsWith(CACHE_STR)) {
				cacheRule = rule.replace(CACHE_STR, "");
			}
		}

		
		switch (columnIndex) {
		case 0:
			return fk.getName() == null ? "" : fk.getName();
		case 1:
			List<String> columns = fk.getAttributes();
			StringBuffer bf = new StringBuffer();
			int count = 0;

			for (String column : columns) {
				if (count != 0) {
					bf.append(",");
				}
				bf.append(column);
				count++;
			}

			return bf.toString();
		case 2:
			return refTable;
		case 3:
			//get reference table's PK
			SchemaInfo refSchema = database.getSchemaInfo(refTable);
			List<SchemaInfo> refSupers=SuperClassUtil.getSuperClasses(database, refSchema);
			Constraint refPK = refSchema.getPK(refSupers);
			if (refPK != null) {
				List<String> refPKAttrs = refPK.getAttributes();
				StringBuffer bf2 = new StringBuffer();
				int count2 = 0;

				for (String column : refPKAttrs) {
					if (count2 != 0) {
						bf2.append(",");
					}
					bf2.append(column);
					count2++;
				}

				return bf2.toString();
			}else{
				return null;
			}

		case 4:
			return updateRule;
		case 5:
			return delRule;
		case 6:
			return cacheRule;

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
	}

}
