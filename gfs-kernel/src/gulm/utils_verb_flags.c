/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifdef __linux__
#include <linux/kernel.h>
#include <linux/sched.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#endif /*__linux__*/

#include "gulm_log_msg_bits.h"

static __inline__ int
strncasecmp (const char *s1, const char *s2, size_t l)
{
	char c1 = '\0', c2 = '\0';

	while (*s1 && *s2 && l-- > 0) {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';

		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';

		if (c1 != c2)
			break;
	}
	return (c1 - c2);
}

static int bit_array[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

#define BITCOUNT(x) (bit_array[x & 0x000F] + \
                     bit_array[(x >> 4) & 0x000F] + \
                     bit_array[(x >> 8) & 0x000F] + \
                     bit_array[(x >> 12) & 0x000F] + \
                     bit_array[(x >> 16) & 0x000F] + \
                     bit_array[(x >> 20) & 0x000F] + \
                     bit_array[(x >> 24) & 0x000F] + \
                     bit_array[(x >> 28) & 0x000F])

struct {
	char *name;
	uint32_t val;
} verbose_flags[] = {
	{
	"Network", lgm_Network,}, {
	"Network2", lgm_Network2,}, {
	"Network3", lgm_Network3,}, {
	"Fencing", lgm_Stomith,}, {
	"Heartbeat", lgm_Heartbeat,}, {
	"Locking", lgm_locking,}, {
	"Forking", lgm_Forking,}, {
	"JIDMap", lgm_JIDMap,}, {
	"JIDUpdates", lgm_JIDUpdates,}, {
	"Subscribers", lgm_Subscribers,}, {
	"LockUpdates", lgm_LockUpdates,}, {
	"LoginLoops", lgm_LoginLoops,}, {
	"ServerState", lgm_ServerState,}, {
	"Default", lgm_Network | lgm_Stomith | lgm_Forking,},
/* Since I really don't want people really doing *all* flags with all,
 * there is AlmostAll, which users really get, and ReallyAll, which is all
 * bits on.
 * This is mostly due to Network3, which dumps messages on nearly
 * every packet. (should actually be every packet.)
 * Also drop the slave updates, since that is on every packet as well.
 */
	{
	"All",
		    (lgm_ReallyAll &
			     ~(lgm_Network3 | lgm_JIDUpdates |
				       lgm_LockUpdates)),}, {
	"AlmostAll",
		    lgm_ReallyAll & ~(lgm_Network3 | lgm_JIDUpdates |
					      lgm_LockUpdates),}, {
	"ReallyAll", lgm_ReallyAll,}
};

static int
add_string (char *name, size_t * cur, char *str, size_t slen)
{
	size_t nl;

	nl = strlen (name);
	if (*cur + nl > slen) {
		memcpy (str + *cur, "...", 3);
		cur += 3;
		str[*cur] = '\0';
		return -1;
	}
	memcpy (str + *cur, name, nl);
	*cur += nl;
	str[*cur] = ',';
	*cur += 1;

	return 0;
}

/**
 * get_verbosity_string - 
 * @str: 
 * @verb: 
 * 
 * 
 * Returns: int
 */
int
get_verbosity_string (char *str, size_t slen, uint32_t verb)
{
	int i, vlen = sizeof (verbose_flags) / sizeof (verbose_flags[0]);
	size_t cur = 0;
	int combo_match = -1, error = 0;

	memset (str, 0, slen);
	slen -= 4;		/* leave room for dots and null */

	if (verb == 0) {
		error = add_string ("Quiet", &cur, str, slen);
		goto end;
	}

	/* Combo verb flag phase */
	for (i = 0; i < vlen; i++) {
		if (BITCOUNT (verbose_flags[i].val) > 1) {
			/* check to see if this flag matches exclusively */
			if ((verbose_flags[i].val ^ verb) == 0) {
				error =
				    add_string (verbose_flags[i].name, &cur,
						str, slen);
				goto end;
			}

			if ((verbose_flags[i].val & verb) ==
			    verbose_flags[i].val) {
				if (combo_match < 0) {
					combo_match = i;
				} else {
					/* Compare this combo with the one in combo_match */
					if (BITCOUNT (verbose_flags[i].val) >
					    BITCOUNT (verbose_flags
						      [combo_match].val)) {
						combo_match = i;
					}
				}

			}
		}
	}
	/* Add the best combo to the string */
	if (combo_match > -1) {
		if (add_string
		    (verbose_flags[combo_match].name, &cur, str, slen) == -1) {
			error = -1;
			goto end;
		}
	}

	/* Single verb flag phase */
	for (i = 0; i < vlen; i++) {
		if (BITCOUNT (verbose_flags[i].val) == 1) {
			if (combo_match > -1) {
				if ((verbose_flags[combo_match].
				     val & verbose_flags[i].val) ==
				    verbose_flags[i].val) {
					continue;
				}
			}

			if ((verbose_flags[i].val & verb) ==
			    verbose_flags[i].val) {
				if (add_string
				    (verbose_flags[i].name, &cur, str,
				     slen) == -1) {
					error = -1;
					goto end;
				}
			}
		}
	}
      end:
	/* Clear trailing ',' */
	if (str[cur - 1] == ',') {
		str[cur - 1] = '\0';
	}
	return error;
}

/**
 * set_verbosity - 
 * @str: 
 * @verb: 
 *
 * toggle bits according to the `rules' in the str.
 * str is a list of verb flags. can be prefexed with '+' or '-'
 * No prefix is the same as '+' prefix
 * '+' sets bits
 * '-' unsets bits.
 * special 'clear' unsets all.
 */
void
set_verbosity (char *str, uint32_t * verb)
{
	char *token, *next;
	int i, wl, tl, len = sizeof (verbose_flags) / sizeof (verbose_flags[0]);

	if (str == NULL)
		return;

	wl = strlen (str);
	if (wl == 0)
		return;
	for (token = str, tl = 0; tl < wl &&
	     token[tl] != ',' &&
	     token[tl] != ' ' && token[tl] != '|' && token[tl] != '\0'; tl++) ;
	next = token + tl + 1;

	for (;;) {
		if (token[0] == '-') {
			token++;
			for (i = 0; i < len; i++) {
				if (strncasecmp
				    (token, verbose_flags[i].name, tl) == 0) {
					(*verb) &= ~(verbose_flags[i].val);
				}
			}
		} else if (token[0] == '+') {
			token++;
			for (i = 0; i < len; i++) {
				if (strncasecmp
				    (token, verbose_flags[i].name, tl) == 0) {
					(*verb) |= verbose_flags[i].val;
				}
			}
		} else {
			if (strncasecmp (token, "clear", tl) == 0) {
				(*verb) = 0;
			} else {
				for (i = 0; i < len; i++) {
					if (strncasecmp
					    (token, verbose_flags[i].name,
					     tl) == 0) {
						(*verb) |= verbose_flags[i].val;
					}
				}
			}
		}

		if (next >= str + wl)
			return;
		for (token = next, tl = 0;
		     tl < wl &&
		     token[tl] != ',' &&
		     token[tl] != ' ' &&
		     token[tl] != '|' && token[tl] != '\0'; tl++) ;
		next = token + tl + 1;

	}
}
