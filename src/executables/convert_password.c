#include <stdio.h>
#include "db.h"

#define  CRYPT_BYTE(x)    ((x) & 0xff)
#define PASSWORD_ENCRYPTION_OLD_KEY        "U9a$y1@zw~a0%"

int seed[3];
int p[256], s[256], q[256];

static int rand8(int i);
static void shuffle(int *a, int size, int rstream);
static int crypt_decode_caps_old(const char *crypt, unsigned char *decrypt,
                                  int maxlen);
static int crypt_unscramble_old(unsigned char *line, int len,
                            unsigned char *decrypt, int maxlen);

static int io_relseek_old (const char *pass, int has_prefix, char *dest);
static int crypt_unscramble_old(unsigned char *line, int len,
                            unsigned char *decrypt, int maxlen);
static int crypt_decode_caps_old(const char *crypt, unsigned char *decrypt,
                                  int maxlen);
static int
crypt_decrypt_printable_old (const char *crypt, char *decrypt, int maxlen);
static void crypt_seed_old (const char *key);

int main(int argc, char * argv[])
{
  float disk_compat_level = 0.0f;
  char * prog_name;
  const char *qp1 = "select db_user, password.password from db_user";
  DB_VALUE user_val, password_val;
  MOP user_class;
  MOP user;
  char * db_name;
  char * password;
  char * decoded_str;
  char * encoded_str;
  int retval, error;
  DB_QUERY_RESULT * query_result;
  DB_QUERY_ERROR query_error;
  char out_buf[128];

  if (argc < 2) {
    printf("usage : %s databasename\n", argv[0]);
    return 1;
  }

  prog_name = argv[0];
  db_name = argv[1];

  AU_DISABLE_PASSWORDS();

  db_login("dba", NULL);
  db_restart (prog_name, 0, db_name);

  error = db_execute (qp1, &query_result, &query_error);
  if (error > 0) {
    error = db_query_first_tuple (query_result);
    while (error == NO_ERROR)
      {
        retval = db_query_get_tuple_value (query_result, 0, &user_val);
        if (retval != NO_ERROR) {
          printf("%s\n", db_error_string(1));
          return 1;
        }

        retval = db_query_get_tuple_value (query_result, 1, &password_val);
        if (retval != NO_ERROR) {
          printf("%s\n", db_error_string(1));
          return 1;
        }

        if (DB_IS_NULL(&user_val) || DB_IS_NULL(&password_val)) {
          error = db_query_next_tuple (query_result);
          continue;
        }

        user = db_get_object (&user_val);
        password = db_get_string (&password_val);

        retval = io_relseek_old (password, 1, out_buf);
        if (retval != NO_ERROR) {
          printf("%s\n", db_error_string(1));
          return 1;
        }
        
        retval = au_set_password (user, out_buf);
        if (retval != NO_ERROR) {
          printf("%s\n", db_error_string(1));
          return 1;
        }
 
        error = db_query_next_tuple (query_result);
      }
    db_query_end(query_result);
  }

  db_commit_transaction();
  db_shutdown ();
  
  return 0;
}

static int
io_relseek_old (const char *pass, int has_prefix, char *dest)
{
  int error = NO_ERROR;
  char buf[AU_MAX_PASSWORD_BUF];
  int len;

  if (pass == NULL || !strlen (pass))
    {
      strcpy (dest, "");
    }
  else
    {
      crypt_seed_old (PASSWORD_ENCRYPTION_OLD_KEY);
      /*
       * Make sure the destination buffer is larger than actually required,
       * the decryption stuff is sensitive about this. Basically for the
       * scrambled strings, the destination buffer has to be the acutal
       * length rounded up to 8 plus another 8.
       */
      if (has_prefix)
	{
	  len = crypt_decrypt_printable_old (pass + 1, buf, AU_MAX_PASSWORD_BUF);
	}
      else
	{
	  len = crypt_decrypt_printable_old (pass, buf, AU_MAX_PASSWORD_BUF);
	}
      if (len != -1 && strlen (buf) <= AU_MAX_PASSWORD_CHARS)
	{
	  strcpy (dest, buf);
	}
      else
	{
	  error = ER_AU_CORRUPTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
    }
  return error;
}

static void crypt_line(unsigned char *line, int len)
{
  int i, c, r0, r1, r2;

  for (i = 0 ; i < len ; i++) {
    c = line[i];
    r0 = rand8(0);
    r1 = rand8(1);
    r2 = rand8(2);
    c = (c ^ r2) + r0;
    c = q[CRYPT_BYTE(s[CRYPT_BYTE(p[CRYPT_BYTE(c)] + r1)] - r1)];
    c = (c - r0) ^ r2;
    line[i] = c;
  }
}

static int rand8(int i)
{
  seed[i] = seed[i] * 1103515245L + 12345;
  return ((seed[i] >> 19) & 0xff);
}

static void shuffle(int *a, int size, int rstream)
{
  int i, j, temp;

  for (i = size - 1; i >= 0; --i) {
    j = rand8(rstream) % (i + 1);
    temp = a[i];
    a[i] = a[j];
    a[j] = temp;
  }
}

static void
crypt_seed_old (const char *key)
{
  int i, keylen;

  keylen = strlen(key);
  for (i = 0; i < 4; ++i) {
    seed[0] = seed[0] << 8 | key[i];
    seed[1] = seed[1] << 8 | key[(keylen - 1) - i];
    seed[2] = seed[2] << 8 | key[keylen / 2 + i - 2];
  }
  for (i = 0; i < 256; ++i)
    p[i] = i;
  shuffle(p, 256, 0);
  for (i = 0; i < 5; ++i) {
    shuffle(p, 256, 0);
    shuffle(p, 256, 1);
    shuffle(p, 256, 2);
  }
  for (i = 0; i < 256; i += 2)
    s[s[p[i]] = p[i + 1]] = p[i];
  shuffle(p, 256, 1);
  for (i = 0; i < 256; ++i)
    q[p[i]] = i;
}

static int
crypt_decrypt_printable_old (const char *crypt, char *decrypt, int maxlen)
{
  unsigned char *work;
  int total;

  total = crypt_decode_caps_old(crypt, (unsigned char *)decrypt, maxlen);
  if (total != -1) {
    work = (unsigned char *)malloc(total);
    if (work == NULL) {
      total = -1;
    }
    else {
      memcpy(work, decrypt, total);
      total = crypt_unscramble_old(work, total, (unsigned char *)decrypt, maxlen);
      free(work);
    }
    if (total != -1) {
      crypt_line((unsigned char *)decrypt, total);
      decrypt[total] = '\0';
    }
  }

  return(total);
}

static int crypt_decode_caps_old(const char *crypt, unsigned char *decrypt,
                                  int maxlen)
{
  int total, len, actual, i, j, zone, offset;

  total = -1;
  if (maxlen) {
    total = 0;
    if (crypt != NULL) {
      len = strlen(crypt);
      if (!len)
        decrypt[0] = '\0';
      else {
        actual = len / 2;
        if ((actual + 1) > maxlen)
          total = -1;
        else {
          for (i = 0, j = 0 ; i < actual ; i++) {
            zone    = crypt[j++] - 'A';
            offset  = crypt[j++] - 'A';
            decrypt[i] = (zone * 26) + offset;
          }
          decrypt[i] = '\0';
          total = i;
        }
      }
    }
  }
  return(total);
}

static int crypt_unscramble_old(unsigned char *line, int len,
                            unsigned char *decrypt, int maxlen)
{
  int total, pad, psn, bitchar, data, mask, dest;

  total = -1;
  if (len <= maxlen) {
    if (!len)
      total = 0;
    else {
      pad = line[0];
      if (pad >= 8)
        return(-1);     /* bogus padding byte */

      psn = 0;
      while (psn < len) {
        if (psn + 8 >= maxlen)
          return(-1);

        /* initialize the destination chars */
        for (dest = 0 ; dest < 8 ; dest++)
          decrypt[psn + dest] = '\0';

        for (bitchar = 0, mask = 1 ; bitchar < 8 ; bitchar++, mask <<= 1) {
          data = line[1 + psn + bitchar];

          for (dest = 0 ; dest < 8 ; dest++)
            decrypt[psn + dest] |= (((data >> dest) << bitchar) & mask);
        }
        psn += 8;
      }
      total = len - pad - 1;
    }
  }
  return(total);
}

