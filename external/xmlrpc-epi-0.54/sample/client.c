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


/* This is a simple demonstration of an xmlrpc client. This program
 * constructs an xmlrpc request and sends it to stdout. The output
 * is suitable for input to ./server.  Of course in a real program,
 * the client would talk over a network, and would care about the
 * results but we do not. Various output options are available.
 *
 * For usage, see below or execute ./client --help
 *
 * See also ./sample which contains both this and ./server in
 * a standalone program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xmlrpc.h"

void print_help() {
   printf("Usage: client [OPTION VALUE]\n\n");
   printf("\t-help    this help message\n");
   printf("\t-encoding <encoding>     (any standard character encoding)\n");
   printf("\t-escaping <markup | cdata | non-ascii | non-xml | none> (may repeat)\n");
   printf("\t-method <methodname>\n");
   printf("\t-output <request | response | both>\n");
   printf("\t-verbosity <pretty | none | newlines>\n");
   printf("\t-version <xmlrpc | simple | soap>\n");
}

int main(int argc, char **argv)
{
  /* args */
  int i;
  int verbosity = 0;
  int version = 0;
  int escaping = 0;
  int output = 0;
  char *methodName = "method_TestNormal";
  char *encoding = 0;
  XMLRPC_REQUEST request;
  STRUCT_XMLRPC_REQUEST_OUTPUT_OPTIONS call_options;

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
              else if(!strcmp(val, "soap")) {
                 version = 2;
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

  /* Now, let's do some client stuff.... */

  /* create a new request object */
  request = XMLRPC_RequestNew();

  /* Set various xml output options.  Or we could just use defaults. */
  call_options.xml_elem_opts.verbosity = verbosity == 1 ? xml_elem_no_white_space : (verbosity == 2 ? xml_elem_newlines_only : xml_elem_pretty);
  call_options.xml_elem_opts.escaping = escaping;
  call_options.version = (version == 1) ? xmlrpc_version_simple : (version == 2 ? xmlrpc_version_soap_1_1 : xmlrpc_version_1_0);
  call_options.xml_elem_opts.encoding = encoding;
  XMLRPC_RequestSetOutputOptions(request, &call_options);

  /* Set the method name and tell it we are making a request */
  XMLRPC_RequestSetMethodName(request, methodName);
  XMLRPC_RequestSetRequestType(request, xmlrpc_request_call);

  /* Create a vector and assign it to the request object */
  XMLRPC_RequestSetData(request, XMLRPC_CreateVector(NULL, xmlrpc_vector_struct));

  /* Add various data to the request. */
  XMLRPC_VectorAppendString(XMLRPC_RequestGetData(request), "string", "This Is A test", 0);
  XMLRPC_VectorAppendString(XMLRPC_RequestGetData(request), "iso_8859_1", "Encoding > 127 test.  This should be the symbol for 1/4: ¼", 0);
  XMLRPC_VectorAppendInt(XMLRPC_RequestGetData(request), "int", 234);
  XMLRPC_VectorAppendDouble(XMLRPC_RequestGetData(request), "double", 234);
  XMLRPC_VectorAppendDateTime(XMLRPC_RequestGetData(request), "datetime", 0);
  XMLRPC_VectorAppendBase64(XMLRPC_RequestGetData(request), "base64", "Testing Base64", 0);

  {
     /* serialize client request as XML */
     char *outBuf = XMLRPC_REQUEST_ToXML(request, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  if(request) {
     /* Free request */
     XMLRPC_RequestFree(request, 1);
  }
  
  return 0;
}

