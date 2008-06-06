#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "global.h"
#include "gfs_ondisk.h"
#include "osi_list.h"

#include "mkfs_gfs.h"





/**
 * test_locking - Make sure the GFS is set up to use the right lock protocol
 * @lockproto: the lock protocol to mount
 * @locktable: the locktable name
 * @estr: returns the a string describing the error
 * @elen: the length of @estr
 *
 * Returns: 0 if things are ok, -1 on error (with estr set)
 */

int test_locking(char *lockproto, char *locktable, char *estr, unsigned int elen)
{
  char *c;

  if (strcmp(lockproto, "lock_nolock") == 0)
  {
    /*  Nolock is always ok.  */
  }
  else if (strcmp(lockproto, "lock_gulm") == 0 ||
           strcmp(lockproto, "lock_dlm") == 0)
  {
    for (c = locktable; *c; c++)
    {
      if (isspace(*c))
      {
	snprintf(estr, elen, "locktable error: contains space characters");
	return -1;
      }
      if (!isprint(*c))
      {
	snprintf(estr, elen, "locktable error: contains unprintable characters");
	return -1;
      }
    }

    c = strstr(locktable, ":");
    if (!c)
    {
      snprintf(estr, elen, "locktable error: missing colon in the locktable");
      return -1;
    }

    if (c == locktable)
    {
      snprintf(estr, elen, "locktable error: missing cluster name");
      return -1;
    }

    if (c - locktable > 16)
    {
      snprintf(estr, elen, "locktable error: cluster name too long");
      return -1;
    }

    c++;
    if (!c)
    {
      snprintf(estr, elen, "locktable error: missing filesystem name");
      return -1;
    }

    if (strstr(c, ":"))
    {
      snprintf(estr, elen, "locktable error: more than one colon present");
      return -1;
    }

    if (!strlen(c))
    {
      snprintf(estr, elen, "locktable error: missing filesystem name");
      return -1;
    }

    if (strlen(c) > 16)
    {
      snprintf(estr, elen, "locktable error: filesystem name too long");
      return -1;
    }
  }
  else
  {
    snprintf(estr, elen, "lockproto error: %s unknown", lockproto);
    return -1;
  }

  return 0;
}
