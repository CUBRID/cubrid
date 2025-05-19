#include "parser_kv_pair.h"

/* Helper – make one pair */
kv_pair *
kv_pair_make (PT_NODE * k, PT_NODE * v)
{
  kv_pair *p = (kv_pair *) malloc (sizeof (kv_pair));
  if (!p)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PT_NODE));
      return NULL;
    }
  p->key = k;
  p->value = v;
  p->next = NULL;
  return p;
}

kv_pair *
kv_pair_push_back (kv_pair * list, kv_pair * item)
{
  if (!list)
    return item;
  kv_pair *cur = list;
  while (cur->next)
    cur = cur->next;
  cur->next = item;
  return list;
}

kv_pair *
kv_pair_push_front (kv_pair * list, kv_pair * item)
{
  if (!item)
    return list;
  item->next = list;
  return item;
}

int
kv_pair_count (const kv_pair * list)
{
  int n = 0;
  while (list)
    {
      ++n;
      list = list->next;
    }
  return n;
}

PT_NODE *
kv_pair_lookup (const kv_pair * list, const char *key_name)
{
  for (const kv_pair * cur = list; cur; cur = cur->next)
    {

      PT_NODE *key = cur->key;

      if (cur->key && cur->key->node_type == PT_NAME && strcasecmp (cur->key->info.name.original, key_name) == 0)
	{
	  return cur->value;
	}
    }
  return NULL;
}
