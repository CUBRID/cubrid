package nbench.script.parser;

class NToken
{
  final static int TokAdd   = 0;
  final static int TokSub   = 1;
  final static int TokMul   = 2;
  final static int TokDiv   = 3;
  final static int TokMod   = 4;
  final static int TokLpr   = 5;
  final static int TokRpr   = 6;
  final static int TokVal   = 7;
  final static int TokId    = 8;
  final static int TokEof   = 9;
  final static int TokError = 10;
  public static String tokenString(int tok)
  {
    switch(tok)
    {
    case TokAdd: return "+";
    case TokSub: return "-";
    case TokMul: return "*";
    case TokDiv: return "/";
    case TokMod: return "%";
    case TokLpr: return "(";
    case TokRpr: return ")";
    case TokVal: return "val";
    case TokId: return "id";
    case TokEof: return "eof";
    case TokError: 
    default: return "error";
    }
  }
}
