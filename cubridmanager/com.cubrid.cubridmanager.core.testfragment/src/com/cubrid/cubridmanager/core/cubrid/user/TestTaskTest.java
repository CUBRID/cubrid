package com.cubrid.cubridmanager.core.cubrid.user;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.core.broker.model.JobInfo;

public class TestTaskTest extends TestCase{
	boolean isFlag = false;
	List<Class<?>> list = new ArrayList<Class<?>>();

	public void testUpdateMessage() {
//		list.add(UserSendObj.class);
//		list.add(LockInfo.class);
//		list.add(DatabaseTransaction.class);
//		list.add(LockHolders.class);
//		list.add(LockWaiters.class);
//		list.add(DatabaseLockInfo.class);
//lock		
//		list.add(BlockedHolders.class);
//		list.add(DatabaseLockInfo.class);
//		list.add(DatabaseTransaction.class);
//		list.add(DbLotEntry.class);
//		list.add(DbLotInfo.class);
//		list.add(LockHolders.class);
//		
//transaction
//		list.add(LockInfo.class);
//		list.add(LockWaiters.class);

//		list.add(DbTransactionList.class);
//		list.add(KillTransactionList.class);
//		list.add(Transaction.class);
//		list.add(TransactionInfo.class);
		
		
//		list.add(BlockedHolders.class);
//		list.add(DatabaseLockInfo.class);
//		list.add(DatabaseTransaction.class);
//		list.add(DbLotEntry.class);
//		list.add(DbLotInfo.class);
//    dbspace		
//		list.add(AddVolumeDbInfo.class);
//		list.add(AutoAddVolumeLogInfo.class);
//		list.add(AutoAddVolumeLogList.class);
//		list.add(DbSpaceInfo.class);
//		list.add(DbSpaceInfoList.class);
//		list.add(GetAddVolumeStatusInfo.class);
//		list.add(GetAutoAddVolumeInfo.class);
//		list.add(VolumeType.class);
		
		
//		list.add(BackupPlanInfo.class);
//		list.add(QueryLogInfo.class);
//		list.add(QueryLogList.class);
//		list.add(QueryPlanInfo.class);
//		list.add(QueryPlanInfoHelp.class);
//		list.add(AdminLogInfoList.class);
//		list.add(AnalyzeCasLogResultInfo.class);
//		list.add(AnalyzeCasLogResultList.class);
//		list.add(AnalyzeCasLogTopResultInfo.class);
//		list.add(BrokerLogInfoList.class);
//		list.add(BrokerLogInfos.class);
//		list.add(CasLogTopResultInfo.class);
//		list.add(DbLogInfoList.class);
//		list.add(DbLogInfos.class);
//		list.add(GetExecuteCasRunnerResultInfo.class);
//		list.add(LogContentInfo.class);
//		list.add(LogInfo.class);
//		list.add(LogInfoManager.class);
//		list.add(ClassAuthorizations.class);
//		list.add(ClassInfo.class);
//		list.add(Constraint.class);
//		list.add(DBAttribute.class);
//		
//		list.add(DBResolution.class);
//		list.add(SchemaChangeManager.class);
//		list.add(SchemaDDL.class);
		list.add(JobInfo.class);


		
		for (Class c : list) {

			StringBuffer sb = new StringBuffer();
			sb.append("public void testModel").append(c.getSimpleName()).append("(){\n     ").append(c.getSimpleName())
			        .append(" bean= new ").append(c.getSimpleName()).append("();").append("\n");
			Field[] fields = c.getDeclaredFields();
			Method[] methods = c.getMethods();
			Map map=new HashMap();
			for(Method m:Object.class.getMethods()){
				map.put(m.getName(), "");
			}
			for (Field f : fields) {
				String fName = f.getName();
				for (Method m : methods) {
					if (m.getName().equalsIgnoreCase("set" + fName)) {
						sb.append("bean.").append(m.getName()).append("(");
						if (f.getType() == String.class) {
							sb.append("\"" + fName + "\");\n");
							sb.append("assertEquals(bean." + m.getName().replaceFirst("set", "get") + "(), \"" + fName
							        + "\");\n");
							
						} else if (f.getType() == int.class ||f.getType() == float.class|| f.getType() == double.class
						        || f.getType() == Integer.class || f.getType() == Double.class) {
							sb.append(fName.length() + ");\n");
							sb.append("assertEquals(bean." + m.getName().replaceFirst("set", "get") + "(), " + fName.length()
							        + ");\n");
						} else if (f.getType() == boolean.class || f.getType() == Boolean.class) {
							sb.append("false);\n");
							sb.append("assertEquals(bean." + m.getName().replaceFirst("set", "is") + "(), false);\n");
						} else {
							sb.append("\"" + fName + "\");\n");
							sb.append("assertEquals(bean." + m.getName().replaceFirst("set", "get") + "(), \"" + fName
							        + "\");\n");
						}

						if(!map.containsKey(m.getName()))
							map.put(m.getName(), m.getName());
						if(!map.containsKey(m.getName().replaceFirst("set", "get")))
							map.put(m.getName().replaceFirst("set", "get"), m.getName().replaceFirst("set", "get"));
					}else if(m.getName().equalsIgnoreCase("add" + fName)){
						sb.append(" bean.").append(m.getName()).append("(")
						.append("new ").append(m.getParameterTypes()[0].getSimpleName())
						.append("());\n");
						
						sb.append("assertEquals(bean." + m.getName().replaceFirst("add", "get") + "() instanceof List,true);\n");
						if(!map.containsKey(m.getName()))
							map.put(m.getName(), m.getName());
						if(!map.containsKey(m.getName().replaceFirst("add", "get")))
							map.put(m.getName().replaceFirst("add", "get"), m.getName().replaceFirst("add", "get"));
					}
				}

			}
			
			
			for (Method m : methods) {
				if(!map.containsKey(m.getName())){
					sb.append("//bean." + m.getName() + "();\n");
				}
			}
			sb.append("}");
			System.out.print(sb.toString());
		}

	}


}
