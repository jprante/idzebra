/* $Id: orddict.c,v 1.2 2006-02-09 08:31:02 adam Exp $
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

#include <assert.h>
#include <yaz/wrbuf.h>
#include "index.h"

WRBUF zebra_mk_ord_str(int ord, const char *str)
{
    char pref[20];
    WRBUF w = wrbuf_alloc();
    int len;

    assert(ord >= 0);

    len = key_SU_encode(ord, pref);

    wrbuf_write(w, pref, len);
    wrbuf_puts(w, str);
    return w;
}

char *dict_lookup_ord(Dict d, int ord, const char *str)
{
    WRBUF w = zebra_mk_ord_str(ord, str);
    char *rinfo = dict_lookup(d, wrbuf_buf(w));
    wrbuf_free(w, 1);
    return rinfo;
}

int dict_insert_ord(Dict d, int ord, const char *p,
		    int userlen, void *userinfo)
{
    WRBUF w = zebra_mk_ord_str(ord, p);
    int r = dict_insert(d, wrbuf_buf(w), userlen, userinfo);
    wrbuf_free(w, 1);
    return r;
}

int dict_delete_ord(Dict d, int ord, const char *p)
{
    WRBUF w = zebra_mk_ord_str(ord, p);
    int r = dict_delete(d, wrbuf_buf(w));
    wrbuf_free(w, 1);
    return r;
}

int dict_delete_subtree_ord(Dict d, int ord, void *client,
			    int (*f)(const char *info, void *client))
{
    WRBUF w = zebra_mk_ord_str(ord, "");
    int r = dict_delete_subtree(d, wrbuf_buf(w), client, f);
    wrbuf_free(w, 1);
    return r;
}
