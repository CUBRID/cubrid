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
package com.cubrid.cubridmanager.core.cubrid.table.model;

/**
 * store attribute changes in modifying a schema table
 * 
 * @author moulinwang
 * @version 1.0 - 2009-6-4 created by moulinwang
 */
public class SchemeChangeLog {
	private String oldValue;
	private String newValue;
	private SchemeInnerType type;

	// four Constraint type
	public enum SchemeInnerType {
		TYPE_SCHEMA("schema"), TYPE_ATTRIBUTE("attribute"), TYPE_CLASSATTRIBUTE(
				"classattribute"), TYPE_FK("fk"), TYPE_INDEX("index"), //including index,reverse index,unique, reverse unique index		
		TYPE_TABLE_NAME("tablename"), TYPE_OWNER("owner"), TYPE_SUPER_TABLE(
				"supertablename");

		String text = null;

		SchemeInnerType(String text) {
			this.text = text;
		}

		public String getText() {
			return text;
		}

		public static SchemeInnerType eval(String text) {
			SchemeInnerType[] array = SchemeInnerType.values();
			for (SchemeInnerType a : array) {
				if (a.getText().equals(text)) {
					return a;
				}
			}
			return null;
		}
	};

	public SchemeChangeLog(String oldValue, String newValue,
			SchemeInnerType type) {
		super();
		this.oldValue = oldValue;
		this.newValue = newValue;
		this.type = type;
	}

	public String getOldValue() {
		return oldValue;
	}

	public void setOldValue(String oldValue) {
		this.oldValue = oldValue;
	}

	public String getNewValue() {
		return newValue;
	}

	public void setNewValue(String newValue) {
		this.newValue = newValue;
	}

	public SchemeInnerType getType() {
		return type;
	}

	public void setType(SchemeInnerType type) {
		this.type = type;
	}


}
