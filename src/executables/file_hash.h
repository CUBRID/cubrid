/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      file_hash.h: File Hashing
 */

#ifndef _FILE_HASH_H_
#define _FILE_HASH_H_

#ident "$Id$"

#include "config.h"
#include "error_manager.h"
#include "oid.h"

/* hash key and data */
typedef struct fh_info
{
  union
  {
    struct
    {
      OID key;
      char data;
    } fh_oidkey;
    struct
    {
      int key;
      char data;
    } fh_intkey;
  } fh_key_data;
} FH_INFO;

#define fh_oidk_key  fh_key_data.fh_oidkey.key
#define fh_oidk_data fh_key_data.fh_oidkey.data
#define fh_intk_key  fh_key_data.fh_intkey.key
#define fh_intk_data fh_key_data.fh_intkey.data

typedef unsigned int FH_FILE_POS;
#define INVALID_FILE_POS (FH_FILE_POS)~0

/* A hash table entry.  */
typedef struct fh_entry
{
  FH_FILE_POS next;		/* Collision chain */
  FH_INFO info;			/* Key & data */
} FH_ENTRY;

typedef struct fh_entry *FH_ENTRY_PTR;

/* header for a page of fh_entry's */
typedef struct fh_page_hdr
{
  struct fh_page_hdr *next;	/* Next header */
  struct fh_page_hdr *prev;	/* Previous header */
  struct fh_entry *fh_entries;	/* Location of fh_entry's */
  FH_FILE_POS page;		/* Hash file page number */
} FH_PAGE_HDR;

typedef unsigned int (*HASH_FUNC) (const void *info, unsigned int htsize);
typedef int (*CMP_FUNC) (const void *key1, const void *key2);

typedef enum
{
  FH_OID_KEY = 1,
  FH_INT_KEY = 2
} FH_KEY_TYPE;

/* Hash table */
typedef struct fh_table
{
  HASH_FUNC hfun;		/* Hash function */
  CMP_FUNC cmpfun;		/* How to compare keys */
  const char *name;		/* Name of Table */
  FH_PAGE_HDR *pg_hdr;		/* The page headers */
  FH_PAGE_HDR *pg_hdr_last;	/* The last page's header */
  FH_PAGE_HDR *pg_hdr_free;	/* The first free pages's header */
  FH_PAGE_HDR *pg_hdr_alloc;	/* The page header address allocated  */
  int size;			/* Better if prime number */
  int page_size;		/* Number of bytes per page */
  int data_size;		/* Number of bytes of data per entry */
  int entry_size;		/* Number of bytes per entry */
  int entries_per_page;		/* Number of fh_entry's per page */
  int cached_pages;		/* Number of cached pages */
  FH_FILE_POS overflow;		/* Page & entry for overflow */
  int nentries;			/* Actual number of entries */
  int ncollisions;		/* Number of collisions */
  char *hash_filename;		/* Hash file pathname */
  int fd;			/* Hash file file descriptor */
  FH_KEY_TYPE key_type;		/* Type of key */
  char *bitmap;			/* Bitmap for used pages */
  int bitmap_size;		/* Size of page bitmap */
} FH_TABLE;

typedef void *FH_KEY;
typedef void *FH_DATA;

FH_TABLE *fh_create (const char *, int, int, int, const char *, FH_KEY_TYPE,
		     int, HASH_FUNC, CMP_FUNC);
int fh_put (FH_TABLE * ht, FH_KEY key, FH_DATA data);
int fh_get (FH_TABLE * ht, FH_KEY key, FH_DATA * data);
void fh_destroy (FH_TABLE * ht);
void fh_dump (FH_TABLE * ht);

#endif /* _FILE_HASH_H_ */
