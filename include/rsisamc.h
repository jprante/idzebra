/* $Id: rsisamc.h,v 1.10 2004-09-01 15:01:32 heikki Exp $
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



#ifndef RSET_ISAMC_H
#define RSET_ISAMC_H

#include <rset.h>
#include <isamc.h>

#ifdef __cplusplus
extern "C" {
#endif

#error "do not use rsisamc.h any more, rset.h covers it all "
extern const struct rset_control *rset_kind_isamc;

RSET rsisamc_create( NMEM nmem, int key_size, 
            int (*cmp)(const void *p1, const void *p2),
            ISAMC is, ISAMC_P pos);

#ifdef __cplusplus
}
#endif

#endif
