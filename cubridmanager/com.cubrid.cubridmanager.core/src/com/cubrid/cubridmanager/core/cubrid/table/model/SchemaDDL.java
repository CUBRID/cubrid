/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.text.ParseException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.apache.log4j.Logger;

import com.cubrid.cubridmanager.core.CommonTool;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemeChangeLog.SchemeInnerType;

/**
 * to generate DDL of a schema or alter DDL when schema changes
 * 
 * @author moulinwang
 * @version 1.0 - 2009-5-22 created by moulinwang
 */

public class SchemaDDL {

	private static final Logger logger = LogUtil.getLogger(SchemaDDL.class);
	SchemaChangeManager changelogs = null;
	String newLine = null;
	String endLineChar = ";";
	DatabaseInfo database;
	private List<String> existItemList = new ArrayList<String>();
	private List<String> notNullChangedColumn = new ArrayList<String>();

	/**
	 * get changed resolution, just find the new added resolution, for when
	 * sending them to CUBRID, CUBRID would delete unused resolutions
	 * 
	 * @param oldResolutions
	 * @param newResolutions
	 * @return
	 */
	public List<DBResolution> getResolutionChanges(
			List<DBResolution> oldResolutions, List<DBResolution> newResolutions) {
		List<DBResolution> list = new ArrayList<DBResolution>();
		for (DBResolution newResolution : newResolutions) {
			DBResolution r = SuperClassUtil.getResolution(oldResolutions,
					newResolution.getName(), newResolution.getClassName());
			if (r == null) {
				list.add(newResolution);
			} else if (r != null) {
				if (r.getAlias().equals(newResolution.getAlias())) {
					//no change, so do nothing
				} else {
					list.add(newResolution);
				}
			}

		}
		return list;
	}

	/**
	 * get changed super class
	 * 
	 * @param oldSupers
	 * @param newSupers
	 * @return
	 */

	public List<List<String>> getSuperclassChanges(List<String> oldSupers,
			List<String> newSupers) {
		List<List<String>> retList = new ArrayList<List<String>>();
		List<String> removeList = new ArrayList<String>();
		List<String> addList = new ArrayList<String>();
		if (oldSupers == null || oldSupers.size() == 0) {
			if (newSupers == null || newSupers.size() == 0) {
				return retList;
			}
			retList.add(newSupers);
			return retList;
		} else if (newSupers.size() == 0) {
			retList.add(oldSupers);
			return retList;
		}

		List<String> tempList = new ArrayList<String>();
		tempList.addAll(oldSupers);

		for (int i = 0; i < newSupers.size(); i++) {
			String newSuper = newSupers.get(i);
			int index = tempList.indexOf(newSuper);
			if (index == -1) {
				List<String> removeItems = removeItems(tempList, i,
						tempList.size() - 1);
				removeList.addAll(removeItems);
				tempList.add(newSuper);
				addList.add(newSuper);
			} else {
				if (index == i) {
					//it is nice and do nothing
				} else {
					List<String> removeItems = removeItems(tempList, i,
							index - 1);
					removeList.addAll(removeItems);
				}
			}
		}
		if (tempList.size() > newSupers.size()) {
			for (int i = newSupers.size(); i < tempList.size(); i++) {
				removeList.add(tempList.get(i));
			}
		}

		if (removeList.size() > 0) {
			retList.add(removeList);
		}
		if (addList.size() > 0) {
			retList.add(addList);
		}
		return retList;
	}

	/**
	 * remove items in a list, only used for method
	 * {@link #getSuperclassChanges(List, List)}
	 * 
	 * 
	 * @param tempList
	 * @param from
	 * @param end
	 * @return
	 */
	private List<String> removeItems(List<String> tempList, int from, int end) {
		List<String> retList = new ArrayList<String>();
		for (int i = end; i >= from; i--) {
			String str = tempList.remove(i);
			retList.add(str);
		}
		return retList;
	}

	public SchemaDDL(SchemaChangeManager changelogs, DatabaseInfo database) {
		super();
		this.changelogs = changelogs;
		this.newLine = CommonTool.newLine;
		this.database = database;
	}

	/**
	 * return DDL of a schema when creating a new schema, otherwise, return an
	 * alter DDL
	 * 
	 * @param oldSchemaInfo
	 * @param newSchemaInfo
	 * @return
	 */
	public String getDDL(SchemaInfo oldSchemaInfo, SchemaInfo newSchemaInfo) {
		if (changelogs.isNewTableFlag) {
			return getDDL(newSchemaInfo);
		} else {
			return getAlterDDL(oldSchemaInfo, newSchemaInfo);
		}
	}

	/**
	 * return an alter DDL of schema, some changes stored in
	 * changelogs(SchemaChangeManager), others are found by differing old and
	 * new schema objects
	 * 
	 * @param oldSchemaInfo
	 * @param newSchemaInfo
	 * @return
	 */
	public String getAlterDDL(SchemaInfo oldSchemaInfo, SchemaInfo newSchemaInfo) {
		existItemList.clear();
		notNullChangedColumn.clear();
		List<SchemaInfo> oldSupers = SuperClassUtil.getSuperClasses(database,
				oldSchemaInfo);
		List<SchemaInfo> newSupers = SuperClassUtil.getSuperClasses(database,
				newSchemaInfo);
		//old --> new
		Map<String, String> attrMap = new HashMap<String, String>();

		StringBuffer bf = new StringBuffer();
		StringBuffer dropConstraintBF = new StringBuffer();
		String oldTableName = oldSchemaInfo.getClassname();
		String newTableName = newSchemaInfo.getClassname();
		String tableName = oldTableName;
		if (!oldTableName.equals(newTableName)) {
			bf.append(renameTable(oldTableName, newTableName));
			tableName = newTableName;
		}
		List<SchemeChangeLog> attrChanges = changelogs.getClassAttrChangeLogs();
		attrChanges.addAll(changelogs.getAttrChangeLogs());
		for (SchemeChangeLog log : attrChanges) {
			boolean isClassAttr = false;
			if (log.getType().equals(SchemeInnerType.TYPE_CLASSATTRIBUTE)) {
				isClassAttr = true;
			} else {
				isClassAttr = false;
			}
			if (log.getOldValue() == null) { //add [class] column
				DBAttribute newAttr = newSchemaInfo.getDBAttributeByName(
						log.getNewValue(), isClassAttr);

				if (!isClassAttr) {
					Constraint pk = newSchemaInfo.getPK(newSupers);
					List<String> pkAttributes = pk != null ? pk.getAttributes()
							: new ArrayList<String>();
					bf.append(
							addColumn(tableName, newAttr, pkAttributes,
									newSchemaInfo)).append(endLineChar).append(
							newLine);
				} else {
					bf.append(addClassColumn(tableName, newAttr)).append(
							endLineChar).append(newLine);

				}
			} else if (log.getNewValue() == null) { //del [class] column
				DBAttribute newAttr = oldSchemaInfo.getDBAttributeByName(
						log.getOldValue(), isClassAttr);
				String attrName = newAttr.getName();

				if (!isClassAttr) {
					bf.append(dropColumn(tableName, attrName)).append(
							endLineChar).append(newLine);
				} else {
					bf.append(dropClassColumn(tableName, attrName)).append(
							endLineChar).append(newLine);
				}
			} else { //edit column
				DBAttribute oldAttr = oldSchemaInfo.getDBAttributeByName(
						log.getOldValue(), isClassAttr);
				DBAttribute newAttr = newSchemaInfo.getDBAttributeByName(
						log.getNewValue(), isClassAttr);

				String oldColumnName = oldAttr.getName();
				String columnName = oldColumnName;
				String newColumnName = newAttr.getName();
				if (!newColumnName.equals(oldColumnName)) {

					bf.append(
							renameColumnName(tableName, oldColumnName,
									newColumnName, isClassAttr)).append(
							endLineChar).append(newLine);
					columnName = newColumnName;
					attrMap.put(oldColumnName, newColumnName);
				}
				String oldDefault = oldAttr.getDefault();
				String newDefault = newAttr.getDefault();
				boolean defaultChanged = oldDefault == null ? newDefault != null
						: !oldDefault.equals(newDefault);
				if (defaultChanged) {
					if (newDefault == null) {
						newDefault = "null";
					} else {

						try {
							newDefault = DBAttribute.formatValue(
									newAttr.getType(), newDefault);
						} catch (NumberFormatException e) {
							logger.error(e.getMessage());
						} catch (ParseException e) {
							logger.error(e.getMessage());
						}

					}
					bf.append(
							changeDefault(tableName, columnName, newDefault,
									isClassAttr)).append(endLineChar).append(
							newLine);
				}
				boolean oldNotNull = oldAttr.isNotNull();
				boolean newNotNull = newAttr.isNotNull();
				boolean notNullChanged = oldNotNull != newNotNull;
				if (notNullChanged) {
					boolean isChangedByPK = false;
					if (newNotNull) { //add a new PK
						Constraint pk = newSchemaInfo.getPK(newSupers);
						List<String> pkAttributes = pk != null ? pk.getAttributes()
								: new ArrayList<String>();
						if (pkAttributes.contains(newColumnName)) {
							isChangedByPK = true;
						}
					} else { //drop an old PK
						Constraint pk = oldSchemaInfo.getPK(oldSupers);
						List<String> pkAttributes = pk != null ? pk.getAttributes()
								: new ArrayList<String>();
						if (pkAttributes.contains(newColumnName)) {
							isChangedByPK = true;
						}
					}
					if (!isChangedByPK) {
						bf.append("--").append("column \"").append(
								newColumnName).append("\"");
						bf.append("'s notnull constraint:").append(oldNotNull).append(
								" to ").append(newNotNull).append(endLineChar).append(
								newLine);
						notNullChangedColumn.add(newColumnName);
					}
				}
			}
		}
		List<String> oldSuperClasses = oldSchemaInfo.getSuperClasses();
		List<String> newSuperClasses = newSchemaInfo.getSuperClasses();
		List<List<String>> superChanges = getSuperclassChanges(oldSuperClasses,
				newSuperClasses);
		if (superChanges.size() == 0) {
			List<DBResolution> resolutions = getResolutionChanges(
					oldSchemaInfo.getResolutions(),
					newSchemaInfo.getResolutions());
			List<DBResolution> classResolutions = getResolutionChanges(
					oldSchemaInfo.getClassResolutions(),
					newSchemaInfo.getClassResolutions());
			if (resolutions.size() > 0 || classResolutions.size() > 0) {
				bf.append(
						addSuperClass(tableName, null, classResolutions,
								resolutions)).append(endLineChar).append(
						newLine);
			}
		} else {
			List<String> removeItems = null;
			List<String> addItems = null;

			if (superChanges.size() == 1) {
				if (newSuperClasses.size() > oldSuperClasses.size()) {
					addItems = superChanges.get(0);
				} else {
					removeItems = superChanges.get(0);
				}
			} else {
				removeItems = superChanges.get(0);
				addItems = superChanges.get(1);
			}
			if (null != removeItems) {
				bf.append(dropSuperClasses(tableName, removeItems)).append(
						endLineChar).append(newLine);
			}

			if (null != addItems) {
				List<DBResolution> classResolutions = getResolutionChanges(
						oldSchemaInfo.getClassResolutions(),
						newSchemaInfo.getClassResolutions());
				List<DBResolution> resolutions = getResolutionChanges(
						oldSchemaInfo.getResolutions(),
						newSchemaInfo.getResolutions());

				bf.append(
						addSuperClass(tableName, addItems, classResolutions,
								resolutions)).append(endLineChar).append(
						newLine);
			}

		}
		List<SchemaInfo> allSupers = SuperClassUtil.getSuperClasses(database,
				newSchemaInfo);
		allSupers.addAll(newSupers);
		allSupers.addAll(oldSupers);
		Constraint newPK = newSchemaInfo.getPK(allSupers);
		Constraint oldPK = oldSchemaInfo.getPK(oldSupers);

		if (oldPK == null && newPK != null) { //add pk			

			List<String> pkAttributes = newPK != null ? newPK.getAttributes()
					: new ArrayList<String>();
			bf.append(addPK(tableName, pkAttributes)).append(endLineChar).append(
					newLine);

		} else if (oldPK != null && newPK == null) { //del pk
			dropConstraintBF.append(dropPK(tableName, oldPK.getName())).append(
					endLineChar).append(newLine);
		} else if (oldPK != null && newPK != null) { //del and add pk
			Iterator<String> e1 = newPK.getAttributes().iterator();
			Iterator<String> e2 = oldPK.getAttributes().iterator();
			boolean equal = true;
			while (e1.hasNext() && e2.hasNext()) {
				String newAttr = e1.next();
				String oldAttr = e2.next();
				//old attribute should be changed to latest attribute name
				oldAttr = attrMap.get(oldAttr) == null ? oldAttr
						: attrMap.get(oldAttr);
				if (!(newAttr == null ? oldAttr == null
						: newAttr.equals(oldAttr))) {
					equal = false;
					break;
				}
			}
			if (equal) {
				equal = !(e1.hasNext() || e2.hasNext());
			}
			if (!equal) {
				dropConstraintBF.append(dropPK(tableName, oldPK.getName())).append(
						endLineChar).append(newLine);
				List<String> pkAttributes = newPK != null ? newPK.getAttributes()
						: new ArrayList<String>();
				//				if (pkAttributes.size() > 1) {
				bf.append(addPK(tableName, pkAttributes)).append(endLineChar).append(
						newLine);
				//				}
			}
		}

		List<SchemeChangeLog> fkChanges = changelogs.getFKChangeLogs();
		for (SchemeChangeLog log : fkChanges) {
			if (log.getOldValue() == null) { //add fk
				bf.append(addFK(tableName,
						newSchemaInfo.getFKConstraint(log.getNewValue())));
				bf.append(endLineChar).append(newLine);
			} else if (log.getNewValue() == null) { //delete fk				
				dropConstraintBF.append(dropFK(
						tableName,
						oldSchemaInfo.getFKConstraint(log.getOldValue()).getName()));
				dropConstraintBF.append(endLineChar).append(newLine);
			} else {
				Constraint oldFK = oldSchemaInfo.getFKConstraint(log.getOldValue());
				Constraint newFK = newSchemaInfo.getFKConstraint(log.getNewValue());

				Iterator<String> e1 = newFK.getAttributes().iterator();
				Iterator<String> e2 = oldFK.getAttributes().iterator();
				boolean equal = true;
				while (e1.hasNext() && e2.hasNext()) {
					String newAttr = e1.next();
					String oldAttr = e2.next();
					//old attribute should be changed to latest attribute name
					oldAttr = attrMap.get(oldAttr) == null ? oldAttr
							: attrMap.get(oldAttr);
					if (!(newAttr == null ? oldAttr == null
							: newAttr.equals(oldAttr))) {
						equal = false;
						break;
					}
				}
				if (equal) {
					equal = !(e1.hasNext() || e2.hasNext());
				}
				if (!equal) {
					dropConstraintBF.append(dropFK(
							tableName,
							oldSchemaInfo.getFKConstraint(log.getOldValue()).getName()));
					dropConstraintBF.append(endLineChar).append(newLine);

					bf.append(addFK(tableName,
							newSchemaInfo.getFKConstraint(log.getNewValue())));
					bf.append(endLineChar).append(newLine);
				}

			}
		}
		List<SchemeChangeLog> indexChanges = changelogs.getIndexChangeLogs();
		for (SchemeChangeLog log : indexChanges) {
			if (log.getOldValue() == null) { //add index
				String[] strs = log.getNewValue().split("\\$");
				String addIndexDDL = addIndex(tableName,
						newSchemaInfo.getConstraintByName(strs[1]));
				if (!addIndexDDL.equals("")) {
					bf.append(addIndexDDL);
					bf.append(endLineChar).append(newLine);
				}

			} else if (log.getNewValue() == null) { //delete index	
				String[] strs = log.getOldValue().split("\\$");
				dropConstraintBF.append(dropIndex(tableName,
						oldSchemaInfo.getConstraintByName(strs[1])));
				dropConstraintBF.append(endLineChar).append(newLine);
			} else { //modify index
				String[] strs = log.getOldValue().split("$");
				bf.append(dropIndex(tableName,
						oldSchemaInfo.getConstraintByName(strs[1])));
				bf.append(endLineChar).append(newLine);
				strs = log.getNewValue().split("$");
				bf.append(addIndex(tableName,
						newSchemaInfo.getConstraintByName(strs[1])));
				bf.append(endLineChar).append(newLine);

			}
		}
		dropConstraintBF.append(bf.toString());
		return dropConstraintBF.toString();
	}

	/**
	 * return DDL of adding index, unique, reverse index or reverse unique
	 * 
	 * @param tableName
	 * @param indexConstaint
	 * @return
	 */
	private String addIndex(String tableName, Constraint indexConstaint) {
		String type = indexConstaint.getType();
		String defaultName = indexConstaint.getDefaultName(tableName);
		StringBuffer bf = new StringBuffer();
		bf.append("CREATE ");
		if (type.equals("INDEX")) {
			bf.append(" INDEX");
		} else if (type.equals("REVERSE INDEX")) {
			bf.append(" REVERSE INDEX");
		} else if (type.equals("UNIQUE")) {
			bf.append(" UNIQUE INDEX");
		} else if (type.equals("REVERSE UNIQUE")) {
			bf.append(" REVERSE UNIQUE INDEX");
		}
		if (!defaultName.equals(indexConstaint.getName())) {
			bf.append(" ").append("\"").append(indexConstaint.getName()).append(
					"\"");
		}
		bf.append(" ON ").append("\"").append(tableName).append("\"");

		List<String> list = new ArrayList<String>();
		if (type.equals("UNIQUE") || type.equals("INDEX")
				|| type.equals("REVERSE UNIQUE")
				|| type.equals("REVERSE INDEX")) {
			List<String> rules = indexConstaint.getRules();
			for (String rule : rules) {
				String[] strs = rule.split(" ");
				if (strs[1].trim().equals("ASC")) {
					list.add("\"" + strs[0] + "\"");
				} else {
					list.add("\"" + strs[0] + "\" " + strs[1]);
				}
			}

		} else {
			//			List<String> attrs = indexConstaint.getAttributes();
			//			for (String attr : attrs) {
			//				list.add("\"" + attr + "\"");
			//			}

		}
		bf.append("(");
		int count = 0;
		for (String str : list) {
			if (0 != count) {
				bf.append(",");
			}
			bf.append(str);
			count++;
		}
		bf.append(")");
		List<String> attrs = indexConstaint.getAttributes();
		if (attrs.size() == 1 && type.equals("UNIQUE")) {
			String key = attrs.get(0) + "_UNIQUE";
			if (!existItemList.contains(key)) {
				existItemList.add(key);
				return bf.toString();
			} else {
				return "";
			}
		}
		return bf.toString();
	}

	/**
	 * return DDL of dropping index, unique, reverse index or reverse unique
	 * 
	 * @param tableName
	 * @param indexConstaint
	 * @return
	 */
	private String dropIndex(String tableName, Constraint indexConstaint) {
		String type = indexConstaint.getType();
		String defaultName = indexConstaint.getDefaultName(tableName);
		StringBuffer bf = new StringBuffer();
		bf.append("DROP");
		if (type.equals("INDEX")) {
			bf.append(" INDEX");
		} else if (type.equals("REVERSE INDEX")) {
			bf.append(" REVERSE INDEX");
		} else if (type.equals("UNIQUE")) {
			bf.append(" UNIQUE INDEX");
		} else if (type.equals("REVERSE UNIQUE")) {
			bf.append(" REVERSE UNIQUE INDEX");
		}
		if (!defaultName.equals(indexConstaint.getName())) {
			bf.append(" ").append("\"").append(indexConstaint.getName()).append(
					"\"");
		}
		bf.append(" ON ").append("\"").append(tableName).append("\"");

		List<String> list = new ArrayList<String>();
		if (type.equals("UNIQUE") || type.equals("INDEX")) {
			List<String> rules = indexConstaint.getRules();
			for (String rule : rules) {
				String[] strs = rule.split(" ");
				list.add("\"" + strs[0] + "\" " + strs[1]);
			}

		} else if (type.equals("REVERSE UNIQUE")
				|| type.equals("REVERSE INDEX")) {
			List<String> attrs = indexConstaint.getAttributes();
			for (String attr : attrs) {
				list.add("\"" + attr + "\"");
			}
		}
		bf.append("(");
		int count = 0;
		for (String str : list) {
			if (0 != count) {
				bf.append(",");
			}
			bf.append(str);
			count++;
		}
		bf.append(")");
		return bf.toString();
	}

	/**
	 * DDL of adding FK
	 * 
	 * @param tableName
	 * @param fkConstaint
	 * @return
	 */
	private String addFK(String tableName, Constraint fkConstaint) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE ");
		bf.append("\"").append(tableName).append("\"");
		bf.append(" ADD ");
		bf.append(getFKDDL(tableName, fkConstaint));
		return bf.toString();
	}

	/**
	 * DDL of dropping super classes
	 * 
	 * @param tableName
	 * @param superClasses
	 * @return
	 */
	public String dropSuperClasses(String tableName, List<String> superClasses) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE ");
		bf.append("\"").append(tableName).append("\"");
		bf.append(" DROP SUPERCLASS ");
		int count = 0;
		for (String superClass : superClasses) {
			if (count != 0) {
				bf.append(",");
			}
			bf.append("\"").append(superClass).append("\"");
			count++;
		}
		return bf.toString();
	}

	/**
	 * DDL of adding super classes
	 * 
	 * @param tableName
	 * @param superClasses
	 * @param classResolutions
	 * @param resolutions
	 * @return
	 */
	public String addSuperClass(String tableName, List<String> superClasses,
			List<DBResolution> classResolutions, List<DBResolution> resolutions) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE ");
		bf.append("\"").append(tableName).append("\"");
		if (null != superClasses) {
			bf.append(" ADD SUPERCLASS ");
			int count = 0;
			for (String superClass : superClasses) {
				if (count != 0) {
					bf.append(",");
				}
				bf.append("\"").append(superClass).append("\"");
				count++;
			}
		}

		bf.append(getResolutionsDDL(classResolutions, resolutions));
		return bf.toString();
	}

	/**
	 * DDL of resolution part
	 * 
	 * @param classResolutions
	 * @param resolutions
	 * @param bf
	 */
	private String getResolutionsDDL(List<DBResolution> classResolutions,
			List<DBResolution> resolutions) {
		StringBuffer bf = new StringBuffer();
		int count = 0;
		for (DBResolution r : classResolutions) {
			if (count == 0) {
				bf.append(newLine).append("INHERIT ");
			} else {
				bf.append(", ");
			}
			bf.append(getDBResolution(r, true));
			count++;
		}
		for (DBResolution r : resolutions) {
			if (count == 0) {
				bf.append(newLine).append("INHERIT ");
			} else {
				bf.append(", ");
			}
			bf.append(getDBResolution(r, false));
			count++;
		}
		return bf.toString();
	}

	/**
	 * DDL of changing owner
	 * 
	 * @param tableName
	 * @param newOwner
	 */
	public String changeOwner(String tableName, String newOwner) {
		//change_owner (class_name, newOwner)         db_authorizations
		StringBuffer bf = new StringBuffer();
		bf.append(newLine);
		bf.append("call change_owner (");
		bf.append("'").append(tableName).append("',");
		bf.append("'").append(newOwner).append("'");
		bf.append(") on class db_authorizations");
		bf.append(newLine);
		return bf.toString();
	}

	/**
	 * DDL of dropping PK
	 * 
	 * @param tableName
	 * @param pkConstraintName
	 * @return
	 */
	private String dropPK(String tableName, String pkConstraintName) {
		return dropConstraint(tableName, pkConstraintName);
	}

	/**
	 * DDL of dropping FK
	 * 
	 * @param tableName
	 * @param fkConstraintName
	 * @return
	 */
	private String dropFK(String tableName, String fkConstraintName) {
		return dropConstraint(tableName, fkConstraintName);
	}

	/**
	 * DDL of dropping constraint
	 * 
	 * @param tableName
	 * @param constraint
	 * @return
	 */
	private String dropConstraint(String tableName, String constraint) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" DROP CONSTRAINT \"").append(constraint).append("\"");
		return bf.toString();
	}

	/**
	 * DDL of adding PK
	 * 
	 * @param tableName
	 * @param pkAttributes
	 * @return
	 */
	private String addPK(String tableName, List<String> pkAttributes) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" ADD PRIMARY KEY(");
		int count = 0;
		for (String column : pkAttributes) {
			if (count > 0) {
				bf.append(",");
			}
			bf.append("\"").append(column).append("\"");
			count++;
		}
		bf.append(")");
		return bf.toString();
	}

	/**
	 * DDL of changing default value of column
	 * 
	 * @param tableName
	 * @param columnName
	 * @param newDefault
	 * @param isClassAttr
	 * @return
	 */
	private String changeDefault(String tableName, String columnName,
			String newDefault, boolean isClassAttr) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" CHANGE");
		if (isClassAttr) {
			bf.append(" CLASS");
		}
		bf.append(" \"").append(columnName).append("\" DEFAULT ").append(
				newDefault);
		return bf.toString();
	}

	/**
	 * DDL of renaming a column
	 * 
	 * @param tableName
	 * @param oldColumnName
	 * @param newColumnName
	 * @param isClassAttr
	 * @return
	 */
	private String renameColumnName(String tableName, String oldColumnName,
			String newColumnName, boolean isClassAttr) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" RENAME");
		if (isClassAttr) {
			bf.append(" CLASS");
		}
		bf.append(" \"").append(oldColumnName).append("\" AS \"").append(
				newColumnName).append("\"");
		return bf.toString();
	}

	/**
	 * DDL of dropping a class attribute
	 * 
	 * @param tableName
	 * @param attrName
	 * @return
	 */
	private String dropClassColumn(String tableName, String attrName) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" DROP ATTRIBUTE CLASS ");
		bf.append("\"").append(attrName).append("\"");
		return bf.toString();
	}

	/**
	 * DDL of dropping a column
	 * 
	 * @param tableName
	 * @param attrName
	 * @return
	 */
	private String dropColumn(String tableName, String attrName) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" DROP  COLUMN ");
		bf.append("\"").append(attrName).append("\"");
		return bf.toString();
	}

	/**
	 * DDL of adding a class attribute
	 * 
	 * @param tableName
	 * @param newAttr
	 * @return
	 */
	private String addClassColumn(String tableName, DBAttribute newAttr) {
		//add class attribute
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" ADD CLASS ATTRIBUTE ");
		bf.append(this.getClassAttributeDDL(newAttr));
		return bf.toString();
	}

	/**
	 * DDL of adding a column
	 * 
	 * @param tableName
	 * @param newAttr
	 * @param pkAttributes
	 * @param newSchemaInfo
	 * @return
	 */
	public String addColumn(String tableName, DBAttribute newAttr,
			List<String> pkAttributes, SchemaInfo newSchemaInfo) {
		StringBuffer bf = new StringBuffer();
		bf.append("ALTER TABLE \"").append(tableName).append("\"").append(
				" ADD COLUMN ");
		bf.append(getInstanceAttributeDDL(newAttr, pkAttributes, newSchemaInfo));
		//		bf.append(newLine);
		return bf.toString();
	}

	/**
	 * DDL of renaming a table
	 * 
	 * @param oldTableName
	 * @param newTableName
	 * @return
	 */
	private String renameTable(String oldTableName, String newTableName) {
		StringBuffer bf = new StringBuffer();
		bf.append("RENAME CLASS \"").append(oldTableName).append("\" ");
		bf.append("AS \"").append(newTableName).append("\"");
		bf.append(endLineChar).append(newLine);
		return bf.toString();
	}

	/**
	 * return DDL of a schema
	 * 
	 * @param newSchemaInfo
	 * @return
	 */
	public String getDDL(SchemaInfo newSchemaInfo) {
		existItemList.clear();
		StringBuffer bf = new StringBuffer();
		bf.append("CREATE TABLE ");
		String tableName = newSchemaInfo.getClassname();
		if (null == tableName || tableName.equals("")) {
			bf.append("<class_name>");
		} else {
			bf.append("\"").append(tableName).append("\"");
		}

		List<String> slist = newSchemaInfo.getSuperClasses();
		if (slist.size() > 0) {
			bf.append(newLine).append("\t\t UNDER ");
			for (int i = 0; i < slist.size(); i++) {
				if (i != 0) {
					bf.append(",");
				}
				bf.append("\"").append(slist.get(i)).append("\"");
			}
		}
		boolean attrBegin = false;
		int count = 0;
		//class attribute
		List<DBAttribute> clist = newSchemaInfo.getClassAttributes();
		if (clist.size() > 0) {
			for (int i = 0; i < clist.size(); i++) {
				DBAttribute classAttr = clist.get(i);
				String inherit = classAttr.getInherit();
				if (inherit.equalsIgnoreCase(newSchemaInfo.getClassname())) {
					if (count != 0) {
						bf.append(",").append(newLine);
					} else {
						bf.append(newLine);
						attrBegin = true;
						bf.append("CLASS ATTRIBUTE(").append(newLine);
					}
					bf.append(getClassAttributeDDL(classAttr));
					count++;
				}
			}
			if (attrBegin) {
				bf.append(newLine).append(")").append(newLine);
			}
		}
		//instance attribute
		List<SchemaInfo> newSupers = SuperClassUtil.getSuperClasses(database,
				newSchemaInfo);
		Constraint pk = newSchemaInfo.getPK(newSupers);
		List<String> pkAttributes = pk != null ? pk.getAttributes()
				: new ArrayList<String>();
		count = 0;
		attrBegin = false;
		List<DBAttribute> nlist = newSchemaInfo.getAttributes();
		if (nlist.size() > 0) {
			for (int i = 0; i < nlist.size(); i++) {
				DBAttribute instanceAttr = nlist.get(i);
				String inherit = instanceAttr.getInherit();
				if (inherit.equalsIgnoreCase(newSchemaInfo.getClassname())) {
					if (count != 0) {
						bf.append(",").append(newLine);
					} else {
						if (!attrBegin) {
							bf.append("(").append(newLine);
							attrBegin = true;
						}
					}
					bf.append(getInstanceAttributeDDL(instanceAttr,
							pkAttributes, newSchemaInfo));
					count++;
				}
			}
		}
		//constaint
		List<Constraint> constaintList = newSchemaInfo.getConstraints();
		if (constaintList.size() > 0) {
			for (int i = 0; i < constaintList.size(); i++) {
				Constraint constraint = constaintList.get(i);
				List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
						database, newSchemaInfo);
				if (!newSchemaInfo.isInSuperClasses(superList,
						constraint.getName())) {
					String contraintDDL = getContraintDDL(tableName,
							constraint, pkAttributes);
					if (!contraintDDL.equals("")) {
						bf.append(",").append(newLine).append(contraintDDL);
					}

				}

			}
		}
		if (count > 0) {
			bf.append(newLine).append(")");
		}

		String resolutionDDL = getResolutionsDDL(
				newSchemaInfo.getClassResolutions(),
				newSchemaInfo.getResolutions());
		bf.append(resolutionDDL);
		bf.append(endLineChar).append(newLine);

		if (constaintList.size() > 0) {
			for (int i = 0; i < constaintList.size(); i++) {
				Constraint constraint = constaintList.get(i);
				List<SchemaInfo> superList = SuperClassUtil.getSuperClasses(
						database, newSchemaInfo);
				if (!newSchemaInfo.isInSuperClasses(superList,
						constraint.getName())) {
					String type = constraint.getType();
					if (type.equals("UNIQUE") || type.equals("INDEX")
							|| type.equals("REVERSE INDEX")
							|| type.equals("REVERSE UNIQUE")) {
						String indexDDL = addIndex(tableName, constraint);
						if (!indexDDL.equals("")) {
							bf.append(indexDDL);
							bf.append(endLineChar).append(newLine);
						}
					}
				}
			}
		}
		return bf.toString();
	}

	/**
	 * DDL of a resolution
	 * 
	 * @param r
	 * @param isClass
	 * @return
	 */
	private String getDBResolution(DBResolution r, boolean isClass) {
		StringBuffer bf = new StringBuffer();
		if (isClass) {
			bf.append("CLASS ");
		}
		bf.append("\"").append(r.getName()).append("\"");
		bf.append(" OF ").append("\"").append(r.getClassName()).append("\"");
		if (r.getAlias() != null && !r.getAlias().equals("")) {
			bf.append(" AS ").append("\"").append(r.getAlias()).append("\"");
		}
		return bf.toString();
	}

	/**
	 * DDL of a constraint
	 * <li> PK
	 * <li> FK
	 * <li> Unique
	 * 
	 * @param tableName
	 * @param constaint
	 * @param pkAttributes
	 * @return
	 */
	private String getContraintDDL(String tableName, Constraint constaint,
			List<String> pkAttributes) {
		String type = constaint.getType();
		if (pkAttributes.size() != 1 && type.equals("PRIMARY KEY")) {
			return getPKDDL(constaint);
		}
		if (type.equals("FOREIGN KEY")) {
			return getFKDDL(tableName, constaint);
		}
		//		if (type.equals("UNIQUE")) {
		//			if (constaint.getAttributes().size() == 1) {
		//				return "";
		//			} else {
		//				return getUniqueDDL(tableName, constaint);
		//			}
		//		}
		return "";
	}

	/**
	 * DDL of FK in creating and altering a schema
	 * 
	 * @param tableName
	 * @param fkConstaint
	 * @return
	 */
	private String getFKDDL(String tableName, Constraint fkConstaint) {
		StringBuffer bf = new StringBuffer();
		
		String defaultName = fkConstaint.getDefaultName(tableName);
		if (!defaultName.equals(fkConstaint.getName())) {
			bf.append(" \"").append(fkConstaint.getName()).append("\" ");
		}
		
		bf.append("FOREIGN KEY");

		List<String> list = fkConstaint.getAttributes();
		bf.append(" (");
		for (int i = 0; i < list.size(); i++) {
			if (i != 0) {
				bf.append(",");
			}
			bf.append("\"").append(list.get(i)).append("\"");
		}
		bf.append(")");
		List<String> rlist = fkConstaint.getRules();
		String refTable = rlist.get(0).replace("REFERENCES ", "");
		bf.append(" REFERENCES ").append("\"").append(refTable).append("\"");

		bf.append("(");

		SchemaInfo schemaInfo = database.getSchemaInfo(refTable);
		List<SchemaInfo> newSupers = SuperClassUtil.getSuperClasses(database,
				schemaInfo);
		Constraint pkConstaint = schemaInfo.getPK(newSupers);
		List<String> pklist = pkConstaint.getAttributes();
		for (int i = 0; i < pklist.size(); i++) {
			if (i != 0) {
				bf.append(",");
			}
			bf.append("\"").append(pklist.get(i)).append("\"");
		}
		bf.append(")");

		for (int i = 1; i < rlist.size(); i++) {
			String rule = rlist.get(i);
			String tmp = rule.trim().toUpperCase();
			if (tmp.startsWith("ON CACHE OBJECT")) {
				tmp = tmp.replace("ON CACHE OBJECT", "").trim().toLowerCase();
				bf.append(" ON CACHE OBJECT ").append("\"").append(tmp).append(
						"\"");
			} else {
				bf.append(" ").append(rule);
			}
		}
		return bf.toString();
	}

	//	/**
	//	 * DDL of unique in creating a schema
	//	 * 
	//	 * @param tableName
	//	 * @param constaint
	//	 * @return
	//	 */
	//	private String getUniqueDDL(String tableName, Constraint constaint) {
	//		StringBuffer bf = new StringBuffer();
	//		String defaultName = constaint.getDefaultName(tableName);
	//		if (!defaultName.equals(constaint.getName())) {
	//			bf.append("CONSTRAINT \"").append(constaint.getName()).append("\" ");
	//		}
	//		bf.append("UNIQUE(");
	//		List<String> list = constaint.getRules();
	//		for (int i = 0; i < list.size(); i++) {
	//			if (i != 0) {
	//				bf.append(",");
	//			}
	//			bf.append("\"").append(list.get(i)).append("\"");
	//		}
	//		bf.append(")");
	//		return bf.toString();
	//	}

	/**
	 * DDL of PK in creating a schema
	 * 
	 * @param constaint
	 * @return
	 */
	private String getPKDDL(Constraint constaint) {
		StringBuffer bf = new StringBuffer();
		bf.append("PRIMARY KEY(");
		List<String> list = constaint.getAttributes();
		for (int i = 0; i < list.size(); i++) {
			if (i != 0) {
				bf.append(",");
			}
			bf.append("\"").append(list.get(i)).append("\"");
		}
		bf.append(")");
		return bf.toString();
	}

	/**
	 * DDL of a class attribute in creating a schema
	 * 
	 * @param classAttr
	 * @return
	 */
	private String getClassAttributeDDL(DBAttribute classAttr) {
		StringBuffer bf = new StringBuffer();

		bf.append("\"").append(classAttr.getName()).append("\"");
		bf.append(" ").append(classAttr.getType());
		String defaultv = classAttr.getDefault();
		if (defaultv != null) {
			try {
				defaultv = DBAttribute.formatValue(classAttr.getType(),
						defaultv);
			} catch (ParseException e) {
				logger.error(e);
			}
			bf.append(" DEFAULT ").append(defaultv);
		}
		if (classAttr.isNotNull()) {
			bf.append(" ").append("NOT NULL");
		}
		return bf.toString();
	}

	/**
	 * DDL of a attribute in creating a schema
	 * 
	 * @param instanceAttr
	 * @param pkAttributes
	 * @param newSchemaInfo
	 * @return
	 */
	private String getInstanceAttributeDDL(DBAttribute instanceAttr,
			List<String> pkAttributes, SchemaInfo newSchemaInfo) {
		List<SchemaInfo> supers = SuperClassUtil.getSuperClasses(database,
				newSchemaInfo);
		StringBuffer bf = new StringBuffer();
		bf.append("\"").append(instanceAttr.getName()).append("\"");
		bf.append(" ").append(instanceAttr.getType());
		String defaultv = instanceAttr.getDefault();

		if (instanceAttr.isShared()) {
			bf.append(" SHARED ");
			String sharedValue = instanceAttr.getSharedValue();
			if (sharedValue != null) {
				try {
					sharedValue = DBAttribute.formatValue(
							instanceAttr.getType(), sharedValue);
				} catch (ParseException e) {
					logger.error(e);
				}
				bf.append(sharedValue);
			}
		} else if (defaultv != null) {
			try {
				defaultv = DBAttribute.formatValue(instanceAttr.getType(),
						defaultv);
			} catch (ParseException e) {
				logger.error(e);
			}
			bf.append(" DEFAULT ").append(defaultv);
		} else {
			SerialInfo autoInc = instanceAttr.getAutoIncrement();
			if (autoInc != null) {
				bf.append(" AUTO_INCREMENT");
				String seed = autoInc.getMinValue();
				String incrementValue = autoInc.getIncrementValue();
				if (seed != null && incrementValue != null) {
					if (seed.equals("1") && incrementValue.equals("1")) {

					} else {
						bf.append("(");
						bf.append(seed).append(",").append(incrementValue);
						bf.append(")");
					}
				}
			}
		}

		if (pkAttributes.size() == 1
				&& pkAttributes.contains(instanceAttr.getName())) {
			bf.append(" PRIMARY KEY");
			String key = instanceAttr.getName() + "_UNIQUE";
			if (!existItemList.contains(key)) {
				existItemList.add(key);
			}
		} else if (pkAttributes.size() > 1
				&& pkAttributes.contains(instanceAttr.getName())) {
			bf.append(" NOT NULL");
		} else {
			if (instanceAttr.isNotNull()) {
				bf.append(" NOT NULL");
			}
		}
		if (newSchemaInfo.isAttributeUnique(instanceAttr, supers)) {
			String key = instanceAttr.getName() + "_UNIQUE";
			if (!existItemList.contains(key)) {
				existItemList.add(key);
				bf.append(" UNIQUE");
			}
		}
		return bf.toString();
	}

	/**
	 * set the string to end a statement
	 * 
	 * @param endLineChar
	 */
	public void setEndLineChar(String endLineChar) {
		this.endLineChar = endLineChar;
	}

	public List<String> getNotNullChangedColumn() {
		return notNullChangedColumn;
	}
}
