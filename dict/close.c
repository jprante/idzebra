/* $Id: close.c,v 1.8 2004-12-08 12:23:08 adam Exp $
   Copyright (C) 1995,1996,1997,1998,1999,2000,2001,2002
   Index Data Aps

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

#include "dict-p.h"

int dict_close (Dict dict)
{
    assert (dict);

    if (dict->rw)
    {
        void *head_buf;
        dict_bf_readp (dict->dbf, 0, &head_buf);
        memcpy (head_buf, &dict->head, sizeof(dict->head));
        dict_bf_touch (dict->dbf, 0);        
    }
    dict_bf_close (dict->dbf);
    xfree (dict);
    return 0;
}

