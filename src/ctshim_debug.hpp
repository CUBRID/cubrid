/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * ctshim_debug.hpp - for debug
 */

#ifndef _CTSHIM_DEBUG_HPP_
#define _CTSHIM_DEBUG_HPP_



#ident "$Id$"

#include <assert.h>
#include <algorithm>
#include <cinttypes>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "system.h"

class CMyDbgBuf 
{
private:
        int     m_buf_alloced;
        int     m_buf_used;
        int     m_ext_size;
        char*   m_buf;

protected:
        void buffer_expand(int add_size) {
            int new_size = m_buf_alloced;
            int need_size = m_buf_used + add_size;

            if(m_ext_size > 1)
              {                      
                while(need_size > new_size)  
                {
                    new_size += m_ext_size;
                }
              }
            else if(need_size > new_size)
              {
                  new_size = need_size;    
              }

            if(new_size <= m_buf_alloced)
              {
                return;
              }

             m_buf = (m_buf == 0x00) ? (char*)malloc(new_size) : (char*)realloc(m_buf, new_size);
             assert(m_buf);
             m_buf_alloced = new_size;                 
        }        

public:
        CMyDbgBuf() {
                m_buf = 0x00;
                m_buf_used = m_buf_alloced = 0;
                m_ext_size = 4096;
        }
        ~CMyDbgBuf() 
        {
                if(m_buf)
                  {
                        free(m_buf);
                        m_buf = 0x00;
                  }
        }

        char* getbuf_ptr(bool base) { 
                return base ? m_buf : (m_buf + m_buf_used); 
        }
        void check_rest_size(int add_size) {
             if ((m_buf_used + add_size) >= m_buf_alloced)
               {
                   buffer_expand(add_size);
               }               
        }
        int get_rest_size() {
             return m_buf_alloced - m_buf_used;
        }


}; //class CMyDbgBuf 

class CMyDebug
{
private:
        CMyDbgBuf m_cBuf;
        int  m_depth;

protected:

public:
        bool trace_pgbuf;
        UINT64  try_ovfl[8];

public:
        CMyDebug() { m_depth = 0; trace_pgbuf = false; reset_ovfl_counter();}
        ~CMyDebug() {}        
        void incr_depth(int incr) { m_depth += incr;  }
        void set_depth(int depth) { m_depth = depth;  }
        void reset_ovfl_counter() { memset(try_ovfl, 0x00, sizeof(try_ovfl)); }
        void print_ovfl_counter() { fprintf(stdout, "COUNTER=%lu %lu %lu\n", try_ovfl[0], try_ovfl[1], try_ovfl[2]); }
void 
write_message (const char *fmt, ...)
{
  va_list ap;
  int     nLen, size;

  while(1)
  {  
        size = m_cBuf.get_rest_size();
          
        va_start (ap, fmt);        
        nLen = vsnprintf(m_cBuf.getbuf_ptr(false), size, fmt, ap);
        va_end (ap);  
        
         if (nLen >= size)
           { /* try again with more space */
             m_cBuf.check_rest_size(nLen + 1);
           }
         else
            {
                return;
            }  
  }
}

void 
print_function_name (const char *func_name, const char* params, ...)
{ 
   va_list ap;     
   char  tmp_fmt[4096];  
   char  tab_depth[1024] = "\t";
   int i;
 
  for(i = 1; i < m_depth; i++)
    {     
        tab_depth[i] = '\t';           
    }
   tab_depth[i] = 0x00;
  

   if(params && *params)
     {
        sprintf(tmp_fmt, "f> %s%s(%s)\n", tab_depth, func_name, params);

        va_start (ap, tmp_fmt);        
        vfprintf(stdout, tmp_fmt, ap);
        va_end (ap);            
     }
   else
     {
        fprintf(stdout, "f> %s%s()\n", tab_depth, func_name);
     }
}


void 
print_function_info(const char* fmt, ...)
{
   va_list ap;     
   char  tmp_fmt[4096];  
   char  tab_depth[1024] = "\t";
   int i;
 
  for(i = 1; i < m_depth; i++)
    {     
        tab_depth[i] = '\t';           
    }
   tab_depth[i] = 0x00;  

   sprintf(tmp_fmt, "m> %s%s\n", tab_depth, fmt);
   va_start (ap, tmp_fmt);        
   vfprintf(stdout, tmp_fmt, ap);
   va_end (ap);               
}

void 
print_message (const char *fmt, ...)
{
  va_list ap;
  FILE* fp = stdout;

  //fp = fopen("debug.trace", "a+t");         

   va_start (ap, fmt);        
   vfprintf(fp, fmt, ap);
   va_end (ap);            

   //fclose(fp);
}

void 
print_message (const char * file, unsigned int line, const char *function, const char *fmt, ...)
{
  static int base_path_len = 0;
  va_list ap;
  char  tmp_fmt[4096];
  char  tab_depth[1024] = "\t";
  int i;

  if(base_path_len == 0)
    {
        base_path_len = (int)strlen("/home/vagrant/Bld/cubrid/src/");
    }
  for(i = 1; i < m_depth; i++)
    {     
        tab_depth[i] = '\t';           
    }
   tab_depth[i] = 0x00;
  sprintf(tmp_fmt, "%s%s\t %s (%s:%d)\n", tab_depth, function, fmt, file + base_path_len, line);

   va_start (ap, tmp_fmt);        
   vfprintf(stdout, tmp_fmt, ap);
   va_end (ap);            
}

}; // class CMyDebug


#if 0
#define MY_DBG_FUNCTION_OUT(c)    (c).incr_depth(-1)
#define MY_DBG_FUNCTION_IN(c, func, params, ...)  do{ (c).incr_depth(1); (c).print_function_name((func), (params), ##__VA_ARGS__); }while(0)
#define MY_DBG_FUNCTION_INFO(c, fmt, ...)  (c).print_function_info((fmt), ##__VA_ARGS__)
// C99 style
#define MY_DBG_PRINT(c, fmt, ...)  (c).print_message(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)  
// GNU style
//#define MY_DBG_PRINT(c, fmt, args...)  (c).print_message(__FILE__, __LINE__, __func__, fmt, ##args)  

#define MY_DBG_PRINT_X(fmt, ...)  cMyDbg.print_message(fmt, ##__VA_ARGS__)  

#else

#define MY_DBG_FUNCTION_OUT(c)
#define MY_DBG_FUNCTION_IN(c, func, params, ...)
#define MY_DBG_FUNCTION_INFO(c, fmt, ...)  
// C99 style
#define MY_DBG_PRINT(c, fmt, ...)
// GNU style
//#define MY_DBG_PRINT(c, fmt, args...)

#define MY_DBG_PRINT_X(fmt, ...) 
#endif



extern CMyDebug cMyDbg;

#endif // _CTSHIM_DEBUG_HPP_

