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
package com.cubrid.cubridmanager.core.cubrid.table.task;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.socket.TreeNode;
import com.cubrid.cubridmanager.core.cubrid.table.model.DBClasses;
import com.cubrid.cubridmanager.core.cubrid.table.model.SchemaInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.SuperClass;
import com.cubrid.cubridmanager.core.cubrid.trigger.model.Trigger;

/**
 * To parse TreeNode object and return all kinds of model object
 * 
 * @author moulinwang 2009-3-2
 * 
 */
public class ModelUtil {
	/**
	 * String constant in request message
	 */
	// four Constraint type
	public enum ConstraintType{
		INDEX("INDEX"),
		UNIQUE("UNIQUE"),
		REVERSEINDEX("REVERSE INDEX"),
		REVERSEUNIQUE("REVERSE UNIQUE"),
		FOREIGNKEY("FOREIGN KEY"),
		PRIMARYKEY("PRIMARY KEY");
		
		String text=null;
		ConstraintType(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static ConstraintType eval(String text){
			ConstraintType[] array=ConstraintType.values();
			for(ConstraintType a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	};
	// two attribute type
	public enum AttributeCategory{
		INSTANCE("instance"),
		CLASS("class");
		String text;
		AttributeCategory(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static AttributeCategory eval(String text){	
			AttributeCategory[] array=AttributeCategory.values();
			for(AttributeCategory a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	}
	// two status for trigger
	public enum TriggerStatus{
		ACTIVE("ACTIVE"),
		INACTIVE("INACTIVE");
		String text;
		TriggerStatus(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static TriggerStatus eval(String text){			
			return TriggerStatus.valueOf(text);
		}
	}
	// eight event for trigger
	public enum TriggerEvent{
		INSERT("INSERT"),
		UPDATE("UPDATE"),
		DELETE("DELETE"),
		STATEMENTINSERT("STATEMENT INSERT"),
		STATEMENTUPDATE("STATEMENT UPDATE"),
		STATEMENTDELETE("STATEMENT DELETE"),
		COMMIT("COMMIT"),
		ROLLBACK("ROLLBACK");

		String text;
		TriggerEvent(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static TriggerEvent eval(String text){
			TriggerEvent[] array=TriggerEvent.values();
			for(TriggerEvent a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	}
	// four action for trigger
	public enum TriggerAction{
		PRINT("PRINT"),
		REJECT("REJECT"),
		INVALIDATE_TRANSACTION("INVALIDATE TRANSACTION"),
		OTHER_STATEMENT("OTHER STATEMENT");	

		String text;
		TriggerAction(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static TriggerAction eval(String text){
			TriggerAction[] array=TriggerAction.values();
			for(TriggerAction a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	}
	// three action for trigger
	public enum TriggerConditionTime{
		BEFORE("BEFORE"),
		AFTER("AFTER"),
		DEFERRED("DEFERRED");	

		String text;
		TriggerConditionTime(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static TriggerConditionTime eval(String text){			
			return TriggerConditionTime.valueOf(text);
		}
	}
	// two action for trigger
	public enum TriggerActionTime{
		AFTER("AFTER"),
		DEFERRED("DEFERRED");	

		String text;
		TriggerActionTime(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static TriggerActionTime eval(String text){			
			return TriggerActionTime.valueOf(text);
		}
	}
	// two type for class
	public enum ClassType{
		NORMAL("normal"),
		VIEW("view");	

		String text;
		ClassType(String text){
			this.text=text;
		}
		public String getText(){
			return text;
		}
		public static ClassType eval(String text){
			ClassType[] array=ClassType.values();
			for(ClassType a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	}
	
	// two type for class
	public enum YesNoType {
		Y("y"),
		N("n");

		String text = null;

		YesNoType(String text) {
			this.text = text;
		}

		public String getText() {
			return text;
		}
		public static YesNoType eval(String text){	
			YesNoType[] array=YesNoType.values();
			for(YesNoType a:array){
				if(a.getText().equals(text)){
					return a;
				}					
			}
			return null;
		}
	}
	
	public enum KillTranType {
		T("t"),
		U("u"),
		H("h"),
		PG("pg");
		String text = null;

		KillTranType(String text) {
			this.text = text;
		}

		public String getText() {
			return text;
		}
		public static KillTranType eval(String text){			
			return KillTranType.valueOf(text);
		}
	}
	
	public enum OsInfoType {
		// os(NT,LINUX,UNIX)
		NT("NT"),
		LINUX("LINUX"),
		UNIX("UNIX"),
		UNKNOWN("UNKNOWN");
		
		String text = null;
		OsInfoType(String text) {
			this.text = text;
		}

		public String getText() {
			return text;
		}
		public static OsInfoType eval(String text){			
			return OsInfoType.valueOf(text);
		}
	}
	
	/**
	 * Parse triggerlist message TreeNode and return Trigger Object
	 * 
	 * @param triggerinfo
	 *            TreeNode
	 * @return
	 */
	public static List<Trigger> getTriggerList(TreeNode triggerlistnode) {
		List<TreeNode> nodelist=triggerlistnode.getChildren();
		List<Trigger> triggerlist=new ArrayList<Trigger>();
		for(TreeNode triggerinfo: nodelist){
			assert (triggerinfo.getValue("open").equals("triggerinfo"));
			Trigger trigger = new Trigger();
			SocketTask.setFieldValue(triggerinfo, trigger);
			triggerlist.add(trigger);
		}				
		return triggerlist;
	}
	/**
	 * Parse SchemaInfo message TreeNode and return SchemaInfo Object
	 * 
	 * @param classinfo
	 *            TreeNode
	 * @return
	 */
	public static SchemaInfo getSchemaInfo(TreeNode classinfo) {
		assert (classinfo.getValue("open").equals("classinfo"));
		SchemaInfo schema = new SchemaInfo();
		SocketTask.setFieldValue(classinfo, schema);
		return schema;
	}
	/**
	 * Parse SchemaInfo message TreeNode and return SchemaInfo Object
	 * 
	 * @param classinfo
	 *            TreeNode
	 * @return
	 */
	public static DBClasses getClassList(TreeNode classesinfo) {
		assert (classesinfo.getValues("open")[0].equals("systemclass"));
		assert (classesinfo.getValues("open")[1].equals("userclass"));
		DBClasses schema = new DBClasses();
		SocketTask.setFieldValue(classesinfo, schema);
		return schema;
	}
	/**
	 * Parse SuperClass message TreeNode and return SuperClass Object
	 * 
	 * @param superclassnode
	 *            TreeNode
	 * @return
	 */
	public static SuperClass getSuperClass(TreeNode superclassnode) {
		assert (superclassnode.getValue("open").equals("class"));
		SuperClass superclass = new SuperClass();
		SocketTask.setFieldValue(superclassnode, superclass);
		return superclass;
	}
}
