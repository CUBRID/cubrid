package nbench.engine;
import nbench.common.*;
import nbench.common.helper.NValue;
import nbench.common.helper.MapVariableScope;
import nbench.parse.*;
import nbench.report.NStat;
import java.util.List;
import java.util.Properties;
import java.util.Random;
import java.util.Date;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.io.File;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;


/* ------------------------------------------------------------------------- */
public class NFrontEngine {
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
class QueueItem {
/* ------------------------------------------------------------------------- */
  Long tid;
  long st;
  long et;
  int mix_id;
  NStat[] nstat;
  QueueItem(long tid, long st, long et, int mix_id)
  {
    this.tid = new Long(tid);
    this.st = st;
    this.et = et;
    this.mix_id = mix_id;
  }
  void set(NStat[] st) { nstat = st; }
};
/* ------------------------------------------------------------------------- */
class NMonitor extends Thread {
/* ------------------------------------------------------------------------- */
boolean ok_to_report;
private FileOutputStream fos;
NMonitor() throws Exception
{
  ok_to_report = false;
  fos = new FileOutputStream(logdirpath + File.separator + start_time + ".log")
;
}
private void report_stats(QueueItem qi) throws Exception
{
  StringBuffer sb = new StringBuffer();
  sb.append(
  "--\nthread[" + qi.tid + "] interval[" + qi.st + "," + qi.et + "]\n"
  );
  sb.append("mix[" + qi.mix_id + "]\n");
  NStat[] ns = qi.nstat;
  if(ns[0].ntotal > 0) 
    for(int i = 0; i < ns.length; i++)
      ns[i].report(sb, i); 
  fos.write(sb.toString().getBytes());
}
public void run()
{
  long latest_ts = 0L;
  long report_start_time;
  try 
  {
    do 
    {
      while(nstatCLQ.poll() != null)
	;
      if(aliveCount.get() == 0)
	return;
      if(ok_to_report == false)
	Thread.sleep(10);
      else
	break;
    } while(true);
    report_start_time = System.currentTimeMillis();
    do 
    {
      if(aliveCount.get() == 0)
	return;
      QueueItem qi = nstatCLQ.poll();
      if(qi == null)
      { 
	if(aliveCount.get() < 0)
	{
	  fos.flush();
	  fos.close();
	  return;
	}
	Thread.sleep(10);
      }
      else
      {
	report_stats(qi);
      }
    } while (true);
  } 
  catch (Exception e)
  {
    e.printStackTrace();
    System.out.println("ERROR[MON]:" + e.toString());
  }
}
} // end NMonitor
/* ------------------------------------------------------------------------- */
class NClient extends Thread {
/* ------------------------------------------------------------------------- */
private BackendEngine be;
private BackendEngineClient bec;
private NContext ctx;
private long startTime;
private long monitorTime;
private long myTime;
private long endTime;
private FileOutputStream dump_result_os; //leekj 20071212
/**
 */
private void report()
{
  QueueItem qi = new QueueItem(getId(), monitorTime, myTime, ctx.mix_id); 
  NStat[] ns = ctx.getStat();
  qi.set(ns);
  NStat nns[] = new NStat[ns.length];
  for(int i = 0; i < nns.length; i++)
    nns[i] = new NStat(ctx.sigs[i], num_histogram, transaction_fail_timeout);
  ctx.setStat(nns);
  nstatCLQ.add(qi);
}
/**
 */
private Value eval_expr(String expr, VariableScope scope) 
throws NBenchException
{
  //현재는 variable name만 받아 들인다.
  Variable var = scope.getVariable(expr);
  if(var == null)
  {
    throw new NBenchException("error during evaluating expression:["+expr+"]");
  }
  
  return var.getValue();
}
/**
 */
private boolean check_condition(Condition cond, VariableScope scope)
throws NBenchException
{
  boolean term = true;
  for(ConditionItem ci : cond.conds)
  { 
    Value lval = eval_expr(ci.expr, scope);
    Value rval = new NValue(ci.type, ci.value);
    if(ci.op.equals("eq"))
      term = NValue.eq(lval, rval, ci.type);
    else if (ci.op.equals("gt"))
      term = NValue.gt(lval, rval, ci.type);
    else
      throw new NBenchException("unsupported op:" + ci.op);
    if(term != true)
      return false;
  }
  return true;
}
/**
 */
private VariableScope make_arg_scope(FrameScope scope, Transaction tr)
throws NBenchException
{
  Value[] vals = new Value[tr.input_exprs.size()];
  int i = 0;
  for(String expr : tr.input_exprs)
  {
    vals[i++] = eval_expr(expr, scope);
  }
  i = 0;
  HashMap<String, Object> map  = new HashMap<String, Object>();
  for(NameAndType nat : tr.input_args)
  {
    map.put(nat.getName(), vals[i].getAs(nat.getType()));
    i++;
  }
  return new MapVariableScope(map);
}
/**
 */
private void update_frame_scope (FrameScope scope, List<VariableScope> res, 
			Transaction tr) throws NBenchException
{
  for(NameAndType nat : tr.export_map.keySet())
  {
    boolean found = false;
    for(VariableScope s : res)
    {
      Variable var = s.getVariable(nat.getName());
      if(var != null)
      {
	scope.setVariable(var);
	found = true;
	break;
      }
    }
    if(found == false)
      throw new NBenchException("exported symbol not found:" + nat.getName());
  }
}
/**
 */
private void execute_mix() throws Exception
{
  VariableScope ctxscope = ctx.getScope();
  FrameScope scope = new FrameScope(ctxscope);
  NStat[] stats = ctx.getStat();
  List<Step> steps = wl.mixes.get(ctx.mix_id).steps;
  int i = 1;
  NStat stat;
  int mi = ctx.mix_id;

  myTime = System.currentTimeMillis();
  long mix_st = myTime;
  for (Step step : steps)
  {
    boolean skip = false;
    stat = stats[i];
    long step_st = myTime;
    long eTime = 0;
    switch(step.action)
    {
      case Step.NOOP: 
	break;
      case Step.TRANSACTION:
	Transaction tr = wl.trs.get(step.value.toString());
	if(tr.cond != null && check_condition(tr.cond, scope) == false)
	{
	  skip = true;
	  break;
	}
	VariableScope arg_scope = null;
	
	if(tr.input_args.size() > 0)
	{
	  arg_scope =  make_arg_scope(scope, tr);
	}
	if(dump_result_os != null) //leekj 20071212
	{
	  dump_result_os.write((step.val+"=====\n[INPUT]\n").getBytes());
	  if(arg_scope != null)
	  {
	    for (String name : arg_scope.getVariableNames())
	    {
	      Variable var = arg_scope.getVariable(name);
	      String l = "\t" + name + ":" + var.getValue().toString() + "\n";
	      dump_result_os.write(l.getBytes());
	    }
	  }
	}
	//TODO lock time out etc ... --> fail
	try {
          List<VariableScope> res = bec.execute((String)step.value, arg_scope);
          if(dump_result_os != null) //leekj 20071212
          {
            dump_result_os.write("[OUTPUT]\n".getBytes());
            if(res != null)
            {
              int idx = 0;
              for(VariableScope sc : res)
              {
                dump_result_os.write(("[" + idx + "]\n").getBytes());
                for (String name : sc.getVariableNames())
                {
                  Variable var = sc.getVariable(name);
                  String l = "\t" + name + ":";
                  dump_result_os.write(l.getBytes());
                  if(var != null)
                    l = var.getValue().toString() + "\n";
                  else
                    l = "<null>\n";
                  dump_result_os.write(l.getBytes());
                }
                idx++;
              }
            }
          }
          if(tr.export_map.size() > 0)
          {
            update_frame_scope(scope, res, tr);
          }
	}
	catch (Exception e) {
          eTime = System.currentTimeMillis();
          SimpleDateFormat sdfNow = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
          String strNow = sdfNow.format(new Date(eTime));
          System.out.println(strNow + " Thread["+ getId() +"] exception occured. but i'am alive!!!");
	}
	break;
      case Step.SLEEP:
	try { 
	  Thread.sleep(step.intValue); 
	} 
	catch (Exception e) {
	  throw new NBenchException(e.toString());
	}
	break;
      default: 
	throw new NBenchException("Unsupported action:" + step.action);
    } /* end switch */
    i++;
    if (eTime > 0)
    {
      myTime = eTime;
      stat.update((int)(myTime - step_st), 0);
      skip = true;
    }
    else if (eTime == 0 && step.eTime > 0)
    {
      SimpleDateFormat sdfNow = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
      String strNow = sdfNow.format(new Date());
      System.out.println(strNow + " Thread["+ getId() +"] eTime cleared!!!");
      step.eTime = 0;
    }
    if(skip == false)
    {
      myTime = System.currentTimeMillis();
      stat.update((int)(myTime - step_st), transaction_fail_timeout);
     }
    else
      myTime = System.currentTimeMillis();
  } /* end for */
  stats[0].update((int)(myTime - mix_st), Integer.MAX_VALUE);
  
}
public NClient(BackendEngine be, NContext ctx) throws Exception
{
  this.be = be;
  bec = be.createClient();
  this.ctx = ctx;
  myTime = 0;
  //leekj 20071212 -- begin
  if(dumpResult != 0)
    dump_result_os = new FileOutputStream(logdirpath + File.separator + getId());
  //-- end
}
public void run()
{
  try 
  {
    while(okToStart == false)
      Thread.sleep(10);
    startTime = System.currentTimeMillis();
    monitorTime = startTime;
    while(okToRun)
    {
      if(myTime > monitorTime + report_interval)
      {
	report();
	monitorTime = myTime;
      }
      execute_mix();
    }
    endTime = System.currentTimeMillis();
    System.out.println("Thread["+ getId() +"] ends.");
  }
  catch (Exception e)
  {
    e.printStackTrace();
    System.out.println("Thread["+ getId() +"] interrupted." + e.toString());
  }
  finally
  {
    aliveCount.getAndDecrement();
  }
}
} //NClient
/* ------------------------------------------------------------------------- */
/* FIELDS */
/* ------------------------------------------------------------------------- */
private WorkLoad wl;
private Properties props;
private BackendEngine be;
// shared with clients (watch out for concurrency)
boolean okToStart = false;
boolean okToRun = true;
int dumpResult = 0; //leekj 20071212
AtomicInteger aliveCount;
ConcurrentLinkedQueue<QueueItem> nstatCLQ;
String[] mix_ids;
int num_client;
int runtime;
int report_interval;
long start_time;
long end_time;
int num_histogram;
int transaction_fail_timeout;
int warm_up_time;
String basedirpath;
String logdirpath;

/* ------------------------------------------------------------------------- */
/* METHODS */
/* ------------------------------------------------------------------------- */
public NFrontEngine(WorkLoad wl, Properties props) throws NBenchException
{
  this.wl = wl;
  this.props = props;
  this.mix_ids = props.getProperty("mix_ids").split(",");
  this.num_client = this.mix_ids.length;
  this.runtime = Integer.valueOf(props.getProperty("runtime"));
  this.report_interval = Integer.valueOf(props.getProperty("report_interval"));
  this.num_histogram = Integer.valueOf(props.getProperty("num_histogram"));
  this.transaction_fail_timeout = 
	Integer.valueOf(props.getProperty("transaction_fail_timeout"));
  this.warm_up_time = Integer.valueOf(props.getProperty("warm_up_time"));
  //leekj 20071212 -- begin
  this.dumpResult = Integer.valueOf(props.getProperty("dump_result", "0"));
  //--end
}

public void prepare() throws NBenchException
{
  try 
  {
    File basedir;
    File dir; 
    basedir = new File(props.getProperty("basedir"));
    basedir.mkdirs();
    basedirpath = basedir.toString();
    dir = new File(basedirpath + File.separator + "log");
    dir.mkdir();
    logdirpath = dir.toString();
    String ec = props.getProperty("engine");
    be = (BackendEngine)Class.forName(ec).newInstance();
    be.configure(props);
    for (String tr_name : wl.trs.keySet())
    {
      Transaction tr = wl.trs.get(tr_name);
      be.prepareForStatement(tr_name, tr.input_args, tr.output_cols);
    }
    be.consolidateForRun();
  }
  catch (Exception e)
  {
    e.printStackTrace();
    throw new NBenchException("back end engine can't not be initialized:" 
			+ e.toString());
  }
}

public void 
runBenchmark()
throws NBenchException
{
  NMonitor monitor;
  NClient[] clients = new NClient[num_client];
  NContext[] contexts = new NContext[num_client];
  aliveCount = new AtomicInteger(num_client);
  nstatCLQ = new ConcurrentLinkedQueue<QueueItem>();
  start_time = System.currentTimeMillis();
  try {
    // create MON
    monitor = new NMonitor(); 
    System.out.println("start monitor thread[" + monitor.getId() + "]");
    monitor.start();
    // create FEC
    for(int i = 0; i < num_client; i++)
    {
      int mix_id = Integer.valueOf(mix_ids[i]);
      contexts[i] = 
	new NContext(wl, mix_id, num_histogram, transaction_fail_timeout);
      clients[i] = new NClient(be, contexts[i]);
      clients[i].start();
      System.out.println("start client thread[" + clients[i].getId() + "]");
    }
    
    okToStart = true;
    System.out.println("warm-up " + warm_up_time);
    while(warm_up_time-- > 0)
    {
      Thread.sleep(1000);
      System.out.print(".");
    }
    System.out.println("end");
    //
    //TODO check ramp-up, wrap-up time..
    //
    monitor.ok_to_report = true;
    System.out.println("runtime " + runtime);
    while(runtime-- > 0)
    {
      Thread.sleep(1000);
      System.out.print(".");
      if(aliveCount.get() == 0)
	{
    	  System.out.println("aliveCount == 0");
	  break;
	}
    }
    okToRun = false;
    monitor.join();
  }
  catch(Exception e)
  {
    throw new NBenchException(e.toString());
  }
  end_time = System.currentTimeMillis();
  //
  // DO FINAL REPORT
  //
}
/* ------------------------------------------------------------------------- */
}
/* ------------------------------------------------------------------------- */



