/*
  This file is part of libXMLRPC - a C library for xml-encoded function calls.

  Author: Dan Libby (dan@libby.com)
  Epinions.com may be contacted at feedback@epinions-inc.com
*/

/*  
  Copyright 2000 Epinions, Inc. 

  Subject to the following 3 conditions, Epinions, Inc.  permits you, free 
  of charge, to (a) use, copy, distribute, modify, perform and display this 
  software and associated documentation files (the "Software"), and (b) 
  permit others to whom the Software is furnished to do so as well.  

  1) The above copyright notice and this permission notice shall be included 
  without modification in all copies or substantial portions of the 
  Software.  

  2) THE SOFTWARE IS PROVIDED "AS IS", WITHOUT ANY WARRANTY OR CONDITION OF 
  ANY KIND, EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION ANY 
  IMPLIED WARRANTIES OF ACCURACY, MERCHANTABILITY, FITNESS FOR A PARTICULAR 
  PURPOSE OR NONINFRINGEMENT.  

  3) IN NO EVENT SHALL EPINIONS, INC. BE LIABLE FOR ANY DIRECT, INDIRECT, 
  SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES OR LOST PROFITS ARISING OUT 
  OF OR IN CONNECTION WITH THE SOFTWARE (HOWEVER ARISING, INCLUDING 
  NEGLIGENCE), EVEN IF EPINIONS, INC.  IS AWARE OF THE POSSIBILITY OF SUCH 
  DAMAGES.    

*/


/* This program is intended to validate that this implemenation of xmlrpc
 * is compliant with the documented spec at http://www.xmlrpc.org
 *
 * This program reads an XML document from standard input and,
 * if it is a valid XMLRPC request, generates a response to
 * stdout. Various output options are available.
 *
 * Test cases suitable for input are located in ./tests
 *
 * For usage, see below or execute ./server --help
 *
 * See also ./sample which contains both this and ./client in
 * a standalone program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xmlrpc.h"


/*   
 * This handler takes a single parameter, an array of structs, each of which 
 * contains at least three elements named moe, larry and curly, all <i4>s.  
 * Your handler must add all the struct elements named curly and return the 
 * result.  
 */
XMLRPC_VALUE validator1_arrayOfStructsTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xReturn = NULL;

   int iCurly = 0;

   if(xRequest) {
      XMLRPC_VALUE xArray = XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest));
      if(xArray) {
         XMLRPC_VALUE xIter = XMLRPC_VectorRewind(xArray);
         while(xIter) {
            iCurly += XMLRPC_VectorGetIntWithID(xIter, "curly");

            xIter = XMLRPC_VectorNext(xArray);
         }
      }
   }

   xReturn = XMLRPC_CreateValueInt(0, iCurly);

   return xReturn;
}

/*
 * This handler takes a single parameter, a string, that contains any number 
 * of predefined entities, namely <, >, &, ' and ".  
 *
 * Your handler must return a struct that contains five fields, all numbers: 
 * ctLeftAngleBrackets, ctRightAngleBrackets, ctAmpersands, ctApostrophes, 
 * ctQuotes.  
 *
 * To validate, the numbers must be correct.
 */
XMLRPC_VALUE validator1_countTheEntities (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xStruct = XMLRPC_CreateVector(0, xmlrpc_vector_struct);

   int counts[256] = {0};

   //returns struct

   if(xRequest) {
      XMLRPC_VALUE xString = XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest));
      const char* pString = XMLRPC_GetValueString(xString);

      if(pString) {
         const unsigned char* p = (const unsigned char*)pString;
         while(p && *p != 0) {
            counts[*p] ++;
            p++;
         }
      }
   }

   XMLRPC_VectorAppendInt(xStruct, "ctLeftAngleBrackets", counts['<']);
   XMLRPC_VectorAppendInt(xStruct, "ctRightAngleBrackets", counts['>']);
   XMLRPC_VectorAppendInt(xStruct, "ctAmpersands", counts['&']);
   XMLRPC_VectorAppendInt(xStruct, "ctApostrophes", counts['\'']);
   XMLRPC_VectorAppendInt(xStruct, "ctQuotes", counts['"']);

   return xStruct;
}

/*
 * This handler takes a single parameter, a struct, containing at least three 
 * elements named moe, larry and curly, all <i4>s.  Your handler must add the 
 * three numbers and return the result.  
 */
XMLRPC_VALUE validator1_easyStructTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xReturn = NULL;
   int iSum = 0;

   if(xRequest) {
      XMLRPC_VALUE xStruct = XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest));
      if(xStruct) {
         iSum += XMLRPC_VectorGetIntWithID(xStruct, "curly");
         iSum += XMLRPC_VectorGetIntWithID(xStruct, "moe");
         iSum += XMLRPC_VectorGetIntWithID(xStruct, "larry");
      }
   }

   xReturn = XMLRPC_CreateValueInt(0, iSum);

   return xReturn;
}

/*
 * This handler takes a single parameter, a struct.  Your handler must return 
 * the struct.  
 */
XMLRPC_VALUE validator1_echoStructTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xReturn = XMLRPC_CopyValue(XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest)));
   return xReturn;
}

/*
 * This handler takes six parameters, and returns an array containing all the 
 * parameters.  
 */
XMLRPC_VALUE validator1_manyTypesTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xArray = XMLRPC_CreateVector(0, xmlrpc_vector_array);

   if(xRequest && xArray) {
      XMLRPC_VALUE xParams = XMLRPC_RequestGetData(xRequest);
      XMLRPC_VALUE xIter = XMLRPC_VectorRewind(xParams);

      while(xIter) {
         XMLRPC_AddValueToVector(xArray, XMLRPC_CopyValue(xIter));
         xIter = XMLRPC_VectorNext(xParams);
      }
   }

   return xArray;
}

/*
 * This handler takes a single parameter, which is an array containing 
 * between 100 and 200 elements.  Each of the items is a string, your handler 
 * must return a string containing the concatenated text of the first and 
 * last elements.  
 */
XMLRPC_VALUE validator1_moderateSizeArrayCheck (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xReturn = NULL;

   simplestring buf;
   simplestring_init(&buf);

   if(xRequest) {
      XMLRPC_VALUE xArray = XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest));
      if(xArray) {
         XMLRPC_VALUE xIter = XMLRPC_VectorRewind(xArray), xPrev = 0;

         simplestring_add(&buf, XMLRPC_GetValueString(xIter));

         /* TODO: Should add XMLRPC_VectorLast() call.  Much more efficient */
         while(xIter) {
            xPrev = xIter;
            xIter = XMLRPC_VectorNext(xArray);
         }

         simplestring_add(&buf, XMLRPC_GetValueString(xPrev));
      }
   }

   xReturn = XMLRPC_CreateValueString(0, buf.str, buf.len);

   return xReturn;
}

/*
 * This handler takes a single parameter, a struct, that models a daily 
 * calendar.  At the top level, there is one struct for each year.  Each year 
 * is broken down into months, and months into days.  Most of the days are 
 * empty in the struct you receive, but the entry for April 1, 2000 contains 
 * a least three elements named moe, larry and curly, all <i4>s.  Your 
 * handler must add the three numbers and return the result.  
 * 
 * Ken MacLeod: "This description isn't clear, I expected '2000.April.1' when 
 * in fact it's '2000.04.01'.  Adding a note saying that month and day are 
 * two-digits with leading 0s, and January is 01 would help." Done.  
 */
XMLRPC_VALUE validator1_nestedStructTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xReturn = NULL; 
   XMLRPC_VALUE xParams = XMLRPC_RequestGetData(xRequest);

   int iSum = 0;

   XMLRPC_VALUE xStruct = XMLRPC_VectorRewind(xParams);
   XMLRPC_VALUE xYear = XMLRPC_VectorGetValueWithID(xStruct, "2000");
   XMLRPC_VALUE xMonth = XMLRPC_VectorGetValueWithID(xYear, "04");
   XMLRPC_VALUE xDay = XMLRPC_VectorGetValueWithID(xMonth, "01");

   iSum += XMLRPC_VectorGetIntWithID(xDay, "larry");
   iSum += XMLRPC_VectorGetIntWithID(xDay, "curly");
   iSum += XMLRPC_VectorGetIntWithID(xDay, "moe");

   xReturn = XMLRPC_CreateValueInt(0, iSum);

   return xReturn;
}

/*
 * This handler takes one parameter, and returns a struct containing three 
 * elements, times10, times100 and times1000, the result of multiplying the 
 * number by 10, 100 and 1000.  
 */
XMLRPC_VALUE validator1_simpleStructReturnTest (XMLRPC_SERVER server, XMLRPC_REQUEST xRequest, void* userData) {
   XMLRPC_VALUE xStruct = XMLRPC_CreateVector(0, xmlrpc_vector_struct);
   int iIncoming = XMLRPC_GetValueInt(XMLRPC_VectorRewind(XMLRPC_RequestGetData(xRequest)));

   XMLRPC_AddValuesToVector(xStruct,
                            XMLRPC_CreateValueInt("times10", iIncoming * 10),
                            XMLRPC_CreateValueInt("times100", iIncoming * 100),
                            XMLRPC_CreateValueInt("times1000", iIncoming * 1000),
                            NULL);

   return xStruct;
}


void print_help() {
   printf("Usage: server [OPTION VALUE]\n\n");
   printf("\t-help    this help message\n");
   printf("\t-encoding <encoding>     (any standard character encoding)\n");
   printf("\t-escaping <markup | cdata | non-ascii | non-xml | none> (may repeat)\n");
   printf("\t-method <methodname>\n");
   printf("\t-output <xRequest | response | both>\n");
   printf("\t-verbosity <pretty | none | newlines>\n");
   printf("\t-version <xmlrpc | simple>\n");
   printf("\n\tavailable methods:\n"
          "\t\tvalidator1.arrayOfStructsTest\n"
          "\t\tvalidator1.countTheEntities\n"
          "\t\tvalidator1.easyStructTest\n"
          "\t\tvalidator1.echoStructTest\n"
          "\t\tvalidator1.manyTypesTest\n"
          "\t\tvalidator1.moderateSizeArrayCheck\n"
          "\t\tvalidator1.nestedStructTest\n"
          "\t\tvalidator1.simpleStructReturnTest\n");
}


int main(int argc, char **argv)
{
   int i;
   XMLRPC_SERVER  server;
   XMLRPC_REQUEST xRequest=0;
   XMLRPC_REQUEST response;
   STRUCT_XMLRPC_REQUEST_OUTPUT_OPTIONS call_options;

   /* args */
   int verbosity = 0;
   int version = 0;
   int escaping = 0;
   int output = 0;
   char *methodName = "method_TestNormal";
   char *encoding = 0;

   /* for every argument (after the program name) */
   for(i=1; i<argc; i++) {
      char* arg = argv[i];

      if(*arg == '-') {
         char* key = arg + 1;
         char* val = argv[i+1];

         if(key && (!strcmp(key, "help") || !strcmp(key, "-help"))) {
            print_help();
            return 0;
         }

         if(key && val) {
            if(!strcmp(key, "verbosity")) {
               if(!strcmp(val, "pretty")) {
                  verbosity = 0;
               }
               else if(!strcmp(val, "none")) {
                  verbosity = 1;
               }
               else if(!strcmp(val, "newlines")) {
                  verbosity = 2;
               }
            }
            else if(!strcmp(key, "version")) {
               if(!strcmp(val, "xmlrpc")) {
                  version = 0;
               }
               else if(!strcmp(val, "simple")) {
                  version = 1;
               }
            }
            else if(!strcmp(key, "escaping")) {
               if(!strcmp(val, "markup")) {
                  escaping |= xml_elem_markup_escaping ;
               }
               else if(!strcmp(val, "cdata")) {
                  escaping |= xml_elem_cdata_escaping;
               }
               else if(!strcmp(val, "non-ascii")) {
                  escaping |= xml_elem_non_ascii_escaping;
               }
               else if(!strcmp(val, "non-print")) {
                  escaping |= xml_elem_non_print_escaping;
               }
            }
            else if(!strcmp(key, "encoding")) {
               encoding = val;
            }
            else if(!strcmp(key, "output")) {
               if(!strcmp(val, "response")) {
                  output = 0;
               }
               else if(!strcmp(val, "xRequest")) {
                  output = 1;
               }
               else if(!strcmp(val, "both")) {
                  output = 2;
               }
            }
            else if(!strcmp(key, "method")) {
               methodName = val;
            }

            i++;
         }
      }
   }

   /* create a new server object */
   server = XMLRPC_ServerCreate();

   XMLRPC_ServerRegisterMethod(server, "validator1.arrayOfStructsTest", validator1_arrayOfStructsTest);
   XMLRPC_ServerRegisterMethod(server, "validator1.countTheEntities", validator1_countTheEntities);
   XMLRPC_ServerRegisterMethod(server, "validator1.easyStructTest", validator1_easyStructTest);
   XMLRPC_ServerRegisterMethod(server, "validator1.echoStructTest", validator1_echoStructTest);
   XMLRPC_ServerRegisterMethod(server, "validator1.manyTypesTest", validator1_manyTypesTest);
   XMLRPC_ServerRegisterMethod(server, "validator1.moderateSizeArrayCheck", validator1_moderateSizeArrayCheck);
   XMLRPC_ServerRegisterMethod(server, "validator1.nestedStructTest", validator1_nestedStructTest);
   XMLRPC_ServerRegisterMethod(server, "validator1.simpleStructReturnTest", validator1_simpleStructReturnTest);

   /* Now, let's get the client's xRequest from stdin.... */
   {
      char* filebuf[1024 * 100];
      int len = fread(filebuf, sizeof(char), sizeof(filebuf)-1, stdin);
      if(len) {
         filebuf[len] = 0;
         xRequest = XMLRPC_REQUEST_FromXML((const char*)filebuf, len, NULL);
      }
   }

   if(!xRequest) {
      fprintf(stderr, "bogus xmlrpc xRequest\n");
      return 1;
   }


   /* create a response struct */
   response = XMLRPC_RequestNew();
   XMLRPC_RequestSetRequestType(response, xmlrpc_request_response);

   /* Set various xml output options.  Or we could just use defaults. */
   call_options.xml_elem_opts.verbosity = verbosity == 1 ? xml_elem_no_white_space : (verbosity == 2 ? xml_elem_newlines_only : xml_elem_pretty);
   call_options.xml_elem_opts.escaping = escaping;
   call_options.version = (version == 1) ? xmlrpc_version_simple : xmlrpc_version_1_0;
   call_options.xml_elem_opts.encoding = encoding;
   XMLRPC_RequestSetOutputOptions(response, &call_options);

   /* call server method with client xRequest and assign the response to our response struct */
   XMLRPC_RequestSetData(response, XMLRPC_ServerCallMethod(server, xRequest, NULL));


   if(output == 1 || output == 2) {
      /* serialize client request as XML */
      char *outBuf;
      XMLRPC_RequestSetOutputOptions(xRequest, &call_options);
      outBuf = XMLRPC_REQUEST_ToXML(xRequest, 0);

      if(outBuf) {
         printf("%s\n\n --- \n\n", outBuf);
         free(outBuf);
      }
   }

   if(output == 0 || output == 2) {
      /* serialize server response as XML */
      char *outBuf = XMLRPC_REQUEST_ToXML(response, 0);

      if(outBuf) {
         printf(outBuf);
         free(outBuf);
      }
   }

   if(xRequest) {
      /* Free xRequest */
      XMLRPC_RequestFree(xRequest, 1);
   }
   if(response) {
      /* free response */
      XMLRPC_RequestFree(response, 1);
   }
   if(server) {
      XMLRPC_ServerDestroy(server);
   }

   return 0;
}

