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

import com.cubrid.cubridmanager.core.cubrid.table.model.Constraint;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;

public class IndexTableViewerLabelProvider implements
		ITableLabelProvider {
	SchemaInfo schema = null;

	public IndexTableViewerLabelProvider(SchemaInfo schema) {
		this.schema = schema;
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
		default:
			break;
		}
		return null;
	}

	public String getColumnText(Object element, int columnIndex) {
		Constraint a = (Constraint) element;
		switch (columnIndex) {
		case 0:
			return a.getName();
		case 1:
			return a.getType();
		case 2:
			List<String> columns = a.getAttributes();
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
		case 3:
			List<String> rules = a.getRules();
			StringBuffer rulebf = new StringBuffer();
			int count2 = 0;

			for (String rule : rules) {
				if (count2 != 0) {
					rulebf.append(",");
				}
				rulebf.append(rule);
				count2++;
			}

			return rulebf.toString();						
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
