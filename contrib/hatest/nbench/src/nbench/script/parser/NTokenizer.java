package nbench.script.parser;

import nbench.common.ValueType;
import nbench.common.helper.NValue;

class NTokenizer
{
  /**
   */
  private char[] script;
  private int index;
  private Object token_val;
  /**
   */
  public NTokenizer(String script)
  {
    this.script = script.toCharArray();
    this.index = 0;
    this.token_val = null;
  }
  /**
   */
  public int nextToken()
  {
    int tok = NToken.TokError;

    this.token_val = null;
    while(index < script.length && Character.isWhitespace(script[index]))
      index++;
    if(index >= script.length)
      return NToken.TokEof;
    // check op
    char C = script[index++];
    switch(C)
    {
      case '+': return NToken.TokAdd;
      case '-': return NToken.TokSub;
      case '*': return NToken.TokMul;
      case '/': return NToken.TokDiv;
      case '%': return NToken.TokMod;
      case '(': return NToken.TokLpr;
      case ')': return NToken.TokRpr;
      default: break;
    }
    StringBuffer sb = new StringBuffer();
    if(Character.isDigit(C))
    { //check int literal
      sb.append(C);
      while(index < script.length && Character.isDigit(script[index]))
      {
	sb.append(script[index]);
	index++;
      }
      try { //for now only int value is allowed..
	token_val = new NValue(NValue.parseAs(sb.toString(), ValueType.INT));
      }
      catch (Exception e) {
	return NToken.TokError;
      }
      return NToken.TokVal;
    }
    else if (Character.isJavaIdentifierStart(C))
    { //check identifier
      sb.append(C);
      while(index < script.length && 
	    Character.isJavaIdentifierPart(script[index]))
      {
	sb.append(script[index]);
	index++;
      }
      token_val = sb.toString();
      return NToken.TokId;
    }
    else
    {
      index--; 
      return NToken.TokError;
    }
  }
  /**
   */
  public Object tokenVal()
  {
    return token_val;
  }
}
