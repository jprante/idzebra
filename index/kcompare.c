/* $Id: kcompare.c,v 1.59 2006-07-04 14:10:30 adam Exp $
   Copyright (C) 1995-2005
   Index Data ApS

This file is part of the Zebra server.

Zebra is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Zebra is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Zebra; see the file LICENSE.zebra.  If not, write to the
Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "index.h"

#ifdef __GNUC__
#define CODEC_INLINE inline
#else
#define CODEC_INLINE
#endif

void key_logdump_txt(int logmask, const void *p, const char *txt)
{
    struct it_key key;
    if (!txt)
	txt = "(none)";
    if (p)
    {
	char formstr[128];
	int i;

        memcpy (&key, p, sizeof(key));
	assert(key.len > 0 && key.len <= IT_KEY_LEVEL_MAX);
	*formstr = '\0';
	for (i = 0; i<key.len; i++)
	{
	    if (i)
		strcat(formstr, ".");
	    sprintf(formstr + strlen(formstr), ZINT_FORMAT, key.mem[i]);
	}
        yaz_log(logmask, "%s %s", formstr, txt);
    }
    else
        yaz_log(logmask, " (no key) %s",txt);
}

void key_logdump(int logmask, const void *p)
{
    key_logdump_txt(logmask,  p, "");
}

int key_compare_it (const void *p1, const void *p2)
{
    int i, l = ((struct it_key *) p1)->len;
    if (((struct it_key *) p2)->len > l)
	l = ((struct it_key *) p2)->len;
    assert (l <= 4 && l > 0);
    for (i = 0; i < l; i++)
    {
	if (((struct it_key *) p1)->mem[i] != ((struct it_key *) p2)->mem[i])
	{
	    if (((struct it_key *) p1)->mem[i] > ((struct it_key *) p2)->mem[i])
		return l-i;
	    else
		return i-l;
	}
    }
    return 0;
}

char *key_print_it (const void *p, char *buf)
{
    strcpy(buf, "");
    return buf;
}

int key_compare (const void *p1, const void *p2)
{
    struct it_key i1, i2;
    int i, l;
    memcpy (&i1, p1, sizeof(i1));
    memcpy (&i2, p2, sizeof(i2));
    l = i1.len;
    if (i2.len > l)
	l = i2.len;
    assert (l <= 4 && l > 0);
    for (i = 0; i < l; i++)
    {
	if (i1.mem[i] != i2.mem[i])
	{
	    if (i1.mem[i] > i2.mem[i])
		return l-i;
	    else
		return i-l;
	}
    }
    return 0;
}

zint key_get_seq(const void *p)
{
    struct it_key k;
    memcpy (&k, p, sizeof(k));
    return k.mem[k.len-1];
}

zint key_get_segment(const void *p)
{
    struct it_key k;
    memcpy (&k, p, sizeof(k));
    return k.mem[k.len-1] / KEY_SEGMENT_SIZE;
}

int key_qsort_compare (const void *p1, const void *p2)
{
    int r;
    size_t l;
    char *cp1 = *(char **) p1;
    char *cp2 = *(char **) p2;
 
    if ((r = strcmp (cp1, cp2)))
        return r;
    l = strlen(cp1)+1;
    if ((r = key_compare (cp1+l+1, cp2+l+1)))
        return r;
    return cp1[l] - cp2[l];
}

struct iscz1_code_info {
    struct it_key key;
};

void *iscz1_start (void)
{
    struct iscz1_code_info *p = (struct iscz1_code_info *)
	xmalloc (sizeof(*p));
    iscz1_reset(p);
    return p;
}

void key_init(struct it_key *key)
{
    int i;
    key->len = 0;
    for (i = 0; i<IT_KEY_LEVEL_MAX; i++)
	key->mem[i] = 0;
}

void iscz1_reset (void *vp)
{
    struct iscz1_code_info *p = (struct iscz1_code_info *) vp;
    int i;
    p->key.len = 0;
    for (i = 0; i< IT_KEY_LEVEL_MAX; i++)
	p->key.mem[i] = 0;
}

void iscz1_stop (void *p)
{
    xfree (p);
}

/* small encoder that works with unsigneds of any length */
static CODEC_INLINE void iscz1_encode_int (zint d, char **dst)
{
    unsigned char *bp = (unsigned char*) *dst;

    while (d > 127)
    {
        *bp++ = (unsigned) (128 | (d & 127));
	d = d >> 7;
    }
    *bp++ = (unsigned) d;
    *dst = (char *) bp;
}

/* small decoder that works with unsigneds of any length */
static CODEC_INLINE zint iscz1_decode_int (unsigned char **src)
{
    zint d = 0;
    unsigned char c;
    unsigned r = 0;

    while (((c = *(*src)++) & 128))
    {
        d += ((zint) (c&127) << r);
	r += 7;
    }
    d += ((zint) c << r);
    return d;
}

void iscz1_encode (void *vp, char **dst, const char **src)
{
    struct iscz1_code_info *p = (struct iscz1_code_info *) vp;
    struct it_key tkey;
    zint d;
    int i;

    /*   1
	 3, 2, 9, 12
	 3, 2, 10, 2
	 4, 1
	 
	 if diff is 0, then there is more ...
	 if diff is non-zero, then _may_ be more
    */
    memcpy (&tkey, *src, sizeof(struct it_key));

    /* deal with leader + delta encoding .. */
    d = 0;
    assert(tkey.len > 0 && tkey.len <= 4);
    for (i = 0; i < tkey.len; i++)
    {
	d = tkey.mem[i] - p->key.mem[i];
	if (d || i == tkey.len-1)
	{  /* all have been equal until now, now make delta .. */
	    p->key.mem[i] = tkey.mem[i];
	    if (d > 0)
	    {
		iscz1_encode_int (i + (tkey.len << 3) + 64, dst);
		i++;
		iscz1_encode_int (d, dst);
	    }
	    else
	    {
		iscz1_encode_int (i + (tkey.len << 3), dst);
		}
	    break;
	}
    }
    /* rest uses absolute encoding ... */
    for (; i < tkey.len; i++)
    {
	iscz1_encode_int (tkey.mem[i], dst);
	p->key.mem[i] = tkey.mem[i];
    }
    (*src) += sizeof(struct it_key);
}

void iscz1_decode (void *vp, char **dst, const char **src)
{
    struct iscz1_code_info *p = (struct iscz1_code_info *) vp;
    int i;

    int leader = (int) iscz1_decode_int ((unsigned char **) src);
    i = leader & 7;
    if (leader & 64)
	p->key.mem[i] += iscz1_decode_int ((unsigned char **) src);
    else
	p->key.mem[i] = iscz1_decode_int ((unsigned char **) src);
    p->key.len = (leader >> 3) & 7;
    while (++i < p->key.len)
	p->key.mem[i] = iscz1_decode_int ((unsigned char **) src);
    memcpy (*dst, &p->key, sizeof(struct it_key));
    (*dst) += sizeof(struct it_key);
}

ISAMS_M *key_isams_m (Res res, ISAMS_M *me)
{
    isams_getmethod (me);

    me->compare_item = key_compare;
    me->log_item = key_logdump_txt;

    me->codec.start = iscz1_start;
    me->codec.decode = iscz1_decode;
    me->codec.encode = iscz1_encode;
    me->codec.stop = iscz1_stop;
    me->codec.reset = iscz1_reset;

    me->debug = atoi(res_get_def (res, "isamsDebug", "0"));

    return me;
}

ISAMC_M *key_isamc_m (Res res, ISAMC_M *me)
{
    isamc_getmethod (me);

    me->compare_item = key_compare;
    me->log_item = key_logdump_txt;

    me->codec.start = iscz1_start;
    me->codec.decode = iscz1_decode;
    me->codec.encode = iscz1_encode;
    me->codec.stop = iscz1_stop;
    me->codec.reset = iscz1_reset;

    me->debug = atoi(res_get_def (res, "isamcDebug", "0"));

    return me;
}

int key_SU_encode (int ch, char *out)
{
    int i;
    for (i = 0; ch; i++)
    {
	if (ch >= 64)
	    out[i] = 65 + (ch & 63);
	else
	    out[i] = 1 + ch;
	ch = ch >> 6;
    }
    return i;
    /* in   out
       0     1
       1     2
       63    64
       64    65, 2
       65    66, 2
       127   128, 2
       128   65, 3
       191   128, 3
       192   65, 4
    */
}

int key_SU_decode (int *ch, const unsigned char *out)
{
    int len = 1;
    int fact = 1;
    *ch = 0;
    for (len = 1; *out >= 65; len++, out++)
    {
	*ch += (*out - 65) * fact;
	fact <<= 6;
    }
    *ch += (*out - 1) * fact;
    return len;
}

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

