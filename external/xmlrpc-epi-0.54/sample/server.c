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


/* This is a simple demonstration of how to write an xmlrpc server. 
 * This program reads an XML document from standard input and,
 * if it is a valid XMLRPC request, generates a response to
 * stdout. Various output options are available.
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


XMLRPC_VALUE method_echo(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
   return XMLRPC_RequestGetData(request);
}

XMLRPC_VALUE method_TestStruct(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  XMLRPC_VALUE output;
  XMLRPC_VALUE str;
  XMLRPC_VALUE xe_struct;
  const char *cardholder;

  output = XMLRPC_CreateVector(NULL, xmlrpc_vector_array);

  str = XMLRPC_VectorRewind(XMLRPC_RequestGetData(request));

  cardholder = XMLRPC_VectorGetStringWithID(str, "Cardholder");

  xe_struct = XMLRPC_CreateVector(NULL, xmlrpc_vector_struct);

  if(cardholder) {
     XMLRPC_VectorAppendString(xe_struct, "Cardholder", cardholder, 0);
  }

  XMLRPC_VectorAppendString(xe_struct, "Reason", "Whew!!!", 0);
  XMLRPC_VectorAppendString(xe_struct, "Rubadubdub", "Inmytub", 0);
  XMLRPC_AddValueToVector(output, xe_struct);

  return output;
}

XMLRPC_VALUE method_TestArray(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  const char *string;
  char *testing[3] = {
    "One",
    "Two",
    "Three and four",
  };
  int i;
  XMLRPC_VALUE output;
  XMLRPC_VALUE xIter;
 
  output = XMLRPC_CreateVector(NULL, xmlrpc_vector_array);

  xIter = XMLRPC_VectorRewind(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)));
  while(xIter) {
     string = XMLRPC_GetValueString(xIter);

     if(string) {
        XMLRPC_VectorAppendString(output, NULL, string, 0);
     }

     xIter = XMLRPC_VectorNext(XMLRPC_RequestGetData(request));
  }

  for(i = 0; i < 3; i++) {
     XMLRPC_VectorAppendString(output, NULL, testing[i], 0);
  }

  return output;
}

XMLRPC_VALUE method_TestBoolean(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   int iVal = 1;
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "boolean");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_boolean) {
      iVal = XMLRPC_GetValueBoolean(xVal);
   }

   return XMLRPC_CreateValueBoolean(NULL, iVal);
}


XMLRPC_VALUE method_TestInt(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   int iVal = 25;
   XMLRPC_VALUE xParams = XMLRPC_RequestGetData(request);
   XMLRPC_VALUE xArg1Struct = XMLRPC_VectorRewind(xParams);
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(xArg1Struct, "int");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_int) {
      iVal = XMLRPC_GetValueInt(xVal);
   }

   return XMLRPC_CreateValueInt(NULL, iVal);
}

XMLRPC_VALUE method_TestString(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "Hello World";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "string");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_string) {
      pVal = XMLRPC_GetValueString(xVal);
   }

   return XMLRPC_CreateValueString(NULL, pVal, 0);
}

XMLRPC_VALUE method_TestDouble(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   double dVal = 25;
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "double");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_double) {
      dVal = XMLRPC_GetValueDouble(xVal);
   }

   return XMLRPC_CreateValueDouble(NULL, dVal);
}

XMLRPC_VALUE method_TestBase64(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "U29tZUJhc2U2NFN0cmluZw==";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "base64");
   int buf_len = 0;

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_base64) {
      pVal = XMLRPC_GetValueBase64(xVal);
      buf_len = XMLRPC_GetValueStringLen(xVal);
   }

   return XMLRPC_CreateValueBase64(NULL, pVal, buf_len);
}

XMLRPC_VALUE method_TestDateTime(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "19980717T14:08:55";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "datetime");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_datetime) {
      pVal = XMLRPC_GetValueDateTime_ISO8601(xVal);
   }

   return XMLRPC_CreateValueDateTime_ISO8601(NULL, pVal);
}

XMLRPC_VALUE method_TestNormal(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  XMLRPC_VALUE output;

  output = XMLRPC_CreateVector(NULL, xmlrpc_vector_struct);

  XMLRPC_AddValuesToVector(output,
                           method_TestStruct(server, request, userData),
                           method_TestArray(server, request, userData),
                           method_TestBoolean(server, request, userData),
                           method_TestInt(server, request, userData),
                           method_TestString(server, request, userData),
                           method_TestDouble(server, request, userData),
                           method_TestDateTime(server, request, userData),
                           method_TestBase64(server, request, userData),
                           NULL);

  return output;
}

XMLRPC_VALUE method_TestFault(XMLRPC_SERVER server, XMLRPC_REQUEST input, void* userData)
{
   return XMLRPC_UtilityCreateFault(404, "Page Not Found");
}


void print_help() {
   printf("Usage: server [OPTION VALUE]\n\n");
   printf("\t-help    this help message\n");
   printf("\t-encoding <encoding>     (any standard character encoding)\n");
   printf("\t-escaping <markup | cdata | non-ascii | non-print | none> (may repeat)\n");
   printf("\t-method <methodname>\n");
   printf("\t-output <request | response | both>\n");
   printf("\t-verbosity <pretty | none | newlines>\n");
   printf("\t-version <xmlrpc | simple>\n");
   printf("\n\tavailable methods:\n"
          "\t\tmethod_Echo\n" 
          "\t\tmethod_TestNormal\n" 
          "\t\tmethod_TestFault\n"
          "\t\tmethod_TestStruct\n" 
          "\t\tmethod_TestArray\n"
          "\t\tmethod_TestBoolean\n"
          "\t\tmethod_TestInt\n"
          "\t\tmethod_TestString\n"
          "\t\tmethod_TestDouble\n" 
          "\t\tmethod_TestBase64\n"
          "\t\tmethod_TestDateTime\n"
          "\t\tmethod_TestFault\n");
}


int main(int argc, char **argv)
{
  int i;
  XMLRPC_SERVER  server;
  XMLRPC_REQUEST request=0;
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
              else if(!strcmp(val, "request")) {
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

  /* Register some public methods with the server */
  XMLRPC_ServerRegisterMethod(server, "method_Echo", method_echo);
  XMLRPC_ServerRegisterMethod(server, "method_TestNormal", method_TestNormal);
  XMLRPC_ServerRegisterMethod(server, "method_TestFault", method_TestFault);
  XMLRPC_ServerRegisterMethod(server, "method_TestStruct", method_TestStruct);
  XMLRPC_ServerRegisterMethod(server, "method_TestArray", method_TestArray);
  XMLRPC_ServerRegisterMethod(server, "method_TestBoolean", method_TestBoolean);
  XMLRPC_ServerRegisterMethod(server, "method_TestInt", method_TestInt);
  XMLRPC_ServerRegisterMethod(server, "method_TestString", method_TestString);
  XMLRPC_ServerRegisterMethod(server, "method_TestDouble", method_TestDouble);
  XMLRPC_ServerRegisterMethod(server, "method_TestBase64", method_TestBase64);
  XMLRPC_ServerRegisterMethod(server, "method_TestDateTime", method_TestDateTime);

  /* Now, let's get the client's request from stdin.... */
  {
     char filebuf[4096];
     STRUCT_XMLRPC_REQUEST_INPUT_OPTIONS in_opts;
     int len = fread(filebuf, sizeof(char), sizeof(filebuf)-1, stdin);

     if(len) {
        filebuf[len] = 0;
        in_opts.xml_elem_opts.encoding = utf8_get_encoding_id_from_string(encoding);

        request = XMLRPC_REQUEST_FromXML((const char*)filebuf, len, &in_opts);
     }
  }

  if(!request) {
     fprintf(stderr, "bogus xmlrpc request\n");
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

  /* call server method with client request and assign the response to our response struct */
  XMLRPC_RequestSetData(response, XMLRPC_ServerCallMethod(server, request, NULL));

  if(output == 0 || output == 2) {
     /* serialize server response as XML */
     char *outBuf = XMLRPC_REQUEST_ToXML(response, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  if(request) {
     /* Free request */
     XMLRPC_RequestFree(request, 1);
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

