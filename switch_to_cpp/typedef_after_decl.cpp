#include <iostream>
#include <string>
#include <deque>
#include <map>
#include <regex>

using namespace std;

struct Parser{
  deque<string>                       lines;
  map<string, pair<size_t, size_t>>   typedefs_map;
  
  regex                               typedef_regex;
  regex                               word_regex;
  regex                               typedef_regex2;
  regex                               decl_regex;
  regex                               decl_end_regex;
  
  Parser(
  ):  typedef_regex("typedef\\s+(enum)\\s+(\\w+)\\s+(\\w+).*\n"),
      word_regex("(\\S+)"), typedef_regex2("typedef\\s+(enum).*\n"),
      decl_regex("(enum)\\s+(\\w+).*\n"), decl_end_regex(".*?};.*\n"){
  }
  
  void run(istream &_ris, ostream &_ros);
  
  bool match_typedef(const string &_line, string &_name);
  bool match_decl_start(const string &_line, string &_name);
  bool match_decl_end(const string &_line);
};

int main(){
  Parser p;
  
  p.run(cin, cout);
  
  return 0;
}


void Parser::run(istream &_ris, ostream &_ros){
  string  line;
  string  name;
  bool    searching_decl_end = false;
  string  decl_name;
  
  while(!_ris.eof()){
    getline(_ris, line, '\n');
    
    if(!_ris.eof()) line += '\n';
    
    lines.emplace_back(std::move(line));
    
    if(!searching_decl_end){
    
      if(match_typedef(lines.back(), name)){
        auto it = typedefs_map.find(name);
        if(it == typedefs_map.end()){
          typedefs_map[name] = make_pair(lines.size() - 1, 0);
        }else{
          it->second.second = lines.size() - 1;
        }
      }else if(match_decl_start(lines.back(), decl_name)){
        searching_decl_end = true;
      }
    }else if(match_decl_end(lines.back())){
      //cerr<<"blockend found"<<endl;
      searching_decl_end = false;
      auto it = typedefs_map.find(decl_name);
      if(it != typedefs_map.cend()){
        //cerr<<"move typedef: "<<decl_name<<endl;
        lines.emplace_back(lines[it->second.first]);
        lines[it->second.first].clear();
        if(it->second.second){
          lines.emplace_back(lines[it->second.second]);
          lines[it->second.second].clear();
        }
      }else{
        //cerr<<"not found decl: "<<block_name<<endl;
      }
    }else{
      //cerr<<"blockend not found:["<<lines.back()<<']'<<endl;
    }
    
  }
  
  for(const auto& ln: lines){
    if(!ln.empty()){
      _ros<<ln;
    }
    _ros<<flush;
  }
  
}

bool Parser::match_typedef(const string &_line, string &_name){
  if(regex_match(_line, typedef_regex)){
    auto words_begin = std::sregex_iterator(_line.begin(), _line.end(), word_regex);
    auto words_end = std::sregex_iterator();
    sregex_iterator it = words_begin;
    if(it == words_end) return false;
    ++it;//skip typedef
    if(it == words_end) return false;
    ++it;//skip enum/struct
    if(it == words_end) return false;
    std::smatch match = *it;
    _name = match.str();
    //cerr<<"match ["<<_line<<"] as typedef: "<<_name<<endl;
    return true;
  }else{
    //cerr<<"["<<_line<<"] not a typedef"<<endl;
  }
  return false;
}

bool Parser::match_decl_start(const string &_line, string &_name){
  if(regex_match(_line, decl_regex) && !regex_match(_line, typedef_regex2)){
    auto words_begin = std::sregex_iterator(_line.begin(), _line.end(), word_regex);
    auto words_end = std::sregex_iterator();
    sregex_iterator it = words_begin;
    if(it == words_end) return false;
    ++it;//skip enum/struct
    if(it == words_end) return false;
    std::smatch match = *it;
    _name = match.str();
    //cerr<<"match ["<<_line<<"] as definition: "<<_name<<endl;
    return true;
  }else{
    //cerr<<"["<<_line<<"] not a definition start"<<endl;
  }
  return false;
}

bool Parser::match_decl_end(const string &_line){
  //return _line.size() > 2 && _line[0] == '}' && _line[1] == ';';
  return regex_match(_line, decl_end_regex);
}
