package nbench.engine;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.List;
import nbench.common.*;
import nbench.parse.*;
import nbench.report.NStat;

//
// PER THREAD RUNTIME CONTEXT
//
public class NContext
{
  /* ---------------------------------------------------------------------- */
  /* FIELDS */
  /* ---------------------------------------------------------------------- */
  private WorkLoad wl;
  private HashMap<String, SampleVariableScope> svs_hash; //top level samples
  private SampleValue mix_scope_val;
  //per mix nstat (per steps  + 1 for mix itself)
  String[] sigs;
  private NStat[] mix_step_stat;
  private int num_histogram;
  private int tr_timeout;
  int mix_id;
  /* ---------------------------------------------------------------------- */
  /* METHODS */
  /* ---------------------------------------------------------------------- */
  public NContext(WorkLoad wl, int mix_id, int num_histogram, int tr_timeout) 
  throws NBenchException
  {
    this.wl = wl;
    this.num_histogram = num_histogram;
    this.tr_timeout = tr_timeout;
    this.mix_id = mix_id;

    svs_hash = new HashMap<String, SampleVariableScope>();
    for(String s : wl.ss.keySet()) //TODO subset
    {
    	
      svs_hash.put(s, new SampleVariableScope(wl.ss.get(s)));
      
      //test code start
      /*
      SampleVariableScope svs = svs_hash.get(s);
      
      System.out.println("WL : "+ wl.toString());
      System.out.println("MIX: " + s);
      svs.roll();
      for (String name : svs.getVariableNames())
      {
    	  System.out.println("Name: " + svs.getVariable(name).getName() + ",Value : " + svs.getVariable(name).getValue().toString());  
      }
      */
      // test code end
    }
    Mix m = wl.mixes.get(mix_id);
    mix_scope_val = NSampleValue.getSampleValue(ValueType.STRING, m.ss);
    //make sigs
    sigs = new String[m.steps.size() + 1];
    sigs[0] = "mix[" + mix_id + "]";
    //SampleVariableScope svs = svs_hash.get("mix1");
    
    
    for(int i = 1; i < sigs.length; i++)
    {
      Step step = m.steps.get(i-1);
      sigs[i] = step.act + "[" + step.val + "]";
      
    }
    mix_step_stat = new NStat[m.steps.size() + 1];
    for(int i = 0; i < mix_step_stat.length; i++)
    {
      mix_step_stat[i] = new NStat(sigs[i], num_histogram, tr_timeout);
    }
  }
  public NStat[] getStat() { return mix_step_stat; }
  public void setStat(NStat[] ns) { mix_step_stat = ns; }
  public VariableScope getScope()
  {
    SampleValue sv = mix_scope_val;
    SampleVariableScope svs = null;
    if(sv != null)
    {
      String ss = (String)sv.nextValue().getAs(ValueType.STRING);
      svs = svs_hash.get(ss);
      svs.roll();
    }
    return svs;
  }
}
