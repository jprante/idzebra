/*
 * Copyright (c) 1995-2000, Index Data.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation, in whole or in part, for any purpose, is hereby granted,
 * provided that:
 *
 * 1. This copyright and permission notice appear in all copies of the
 * software and its documentation. Notices of copyright or attribution
 * which appear at the beginning of any file must remain unchanged.
 *
 * 2. The names of Index Data or the individual authors may not be used to
 * endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED, OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL INDEX DATA BE LIABLE FOR ANY SPECIAL, INCIDENTAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER OR
 * NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 */

#ifndef D1_MAP_H
#define D1_MAP_H

YAZ_BEGIN_CDECL

typedef struct data1_maptag
{
    int new_field;
    int type;
#define D1_MAPTAG_numeric 1
#define D1_MAPTAG_string 2
    int which;
    union
    {
	int numeric;
	char *string;
    } value;
    struct data1_maptag *next;
} data1_maptag;

typedef struct data1_mapunit
{
    int no_data;
    char *source_element_name;
    data1_maptag *target_path;
    struct data1_mapunit *next;
} data1_mapunit;

typedef struct data1_maptab
{
    char *name;
    oid_value target_absyn_ref;
    char *target_absyn_name;
    data1_mapunit *map;
    struct data1_maptab *next;
} data1_maptab;

YAZ_END_CDECL

#endif
