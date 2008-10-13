/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * getopt.h - 
 */

#ifndef _GETOPT_H_
#define _GETOPT_H_

#ident "$Id$"

extern int getopt (int argc, char *argv[], char *fmt);

extern int optind;
extern char *optarg;

#endif /* _GETOPT_H_ */
