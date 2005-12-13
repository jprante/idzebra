/* $Id: zinfo.c,v 1.53 2005-12-13 13:47:35 adam Exp $
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

#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <idzebra/version.h>
#include "zinfo.h"

#define ZINFO_DEBUG 0

struct zebSUInfo {
    int index_type;
#define ZEB_SU_SET_USE 1
#define ZEB_SU_STR 2
    int which;
    union {
	char *str;
	struct {
	    int set;
	    int use;
	} su;
    } u;
    int ordinal;
};

struct zebSUInfoB {
    struct zebSUInfo info;
    struct zebSUInfoB *next;
};

typedef struct zebAccessObjectB *zebAccessObject;
struct zebAccessObjectB {
    void *handle;
    SYSNO sysno;
    Odr_oid *oid;
    zebAccessObject next;
};

typedef struct zebAccessInfoB *zebAccessInfo;
struct zebAccessInfoB {
    zebAccessObject attributeSetIds;
    zebAccessObject schemas;
};

typedef struct {
    struct zebSUInfoB *SUInfo;
    SYSNO sysno;
    int dirty;
    int readFlag;
    data1_node *data1_tree;
} *zebAttributeDetails;

struct zebDatabaseInfoB {
    zebAttributeDetails attributeDetails;
    int ordinalDatabase;
    char *databaseName;
    data1_node *data1_database;
    zint recordCount;    /* records in db */
    zint recordBytes;    /* size of records */
    SYSNO sysno;         /* sysno of database info */
    int readFlag;        /* 1: read is needed when referenced; 0 if not */
    int dirty;           /* 1: database is dirty: write is needed */
    struct zebDatabaseInfoB *next;
    zebAccessInfo accessInfo;
};

struct zebraExplainAttset {
    char *name;
    int ordinal;
    struct zebraExplainAttset *next;
};

struct zebraCategoryListInfo {
    int dirty;
    SYSNO sysno;
    data1_node *data1_categoryList;
};

struct zebraExplainInfo {
    int ordinalSU;
    int ordinalDatabase;
    zint runNumber;
    int dirty;
    int write_flag;
    Records records;
    data1_handle dh;
    Res res;
    struct zebraExplainAttset *attsets;
    NMEM nmem;
    data1_node *data1_target;
    struct zebraCategoryListInfo *categoryList;
    struct zebDatabaseInfoB *databaseInfo;
    struct zebDatabaseInfoB *curDatabaseInfo;
    zebAccessInfo accessInfo;
    char date[15]; /* YYYY MMDD HH MM SS */
    int (*updateFunc)(void *handle, Record drec, data1_node *n);
    void *updateHandle;
};

static void zebraExplain_initCommonInfo(ZebraExplainInfo zei, data1_node *n);
static void zebraExplain_initAccessInfo(ZebraExplainInfo zei, data1_node *n);

static data1_node *read_sgml_rec(data1_handle dh, NMEM nmem, Record rec)
{
    return data1_read_sgml(dh, nmem, rec->info[recInfo_storeData]);
}

static void zebraExplain_writeDatabase(ZebraExplainInfo zei,
                                        struct zebDatabaseInfoB *zdi,
					int key_flush);
static void zebraExplain_writeAttributeDetails(ZebraExplainInfo zei,
						zebAttributeDetails zad,
						const char *databaseName,
						int key_flush);
static void zebraExplain_writeTarget(ZebraExplainInfo zei, int key_flush);
static void zebraExplain_writeAttributeSet(ZebraExplainInfo zei,
					    zebAccessObject o,
					    int key_flush);
static void zebraExplain_writeCategoryList(ZebraExplainInfo zei,
					    struct zebraCategoryListInfo *zcl,
					    int key_flush);


static Record createRecord(Records records, SYSNO *sysno)
{
    Record rec;
    if (*sysno)
    {
	rec = rec_get(records, *sysno);
	xfree(rec->info[recInfo_storeData]);
    }
    else
    {
	rec = rec_new(records);
	*sysno = rec->sysno;
	
	rec->info[recInfo_fileType] =
	    rec_strdup("grs.sgml", &rec->size[recInfo_fileType]);
	rec->info[recInfo_databaseName] =
	    rec_strdup("IR-Explain-1",
			&rec->size[recInfo_databaseName]); 
    }
    return rec;
}

void zebraExplain_flush(ZebraExplainInfo zei, void *handle)
{
    if (!zei)
        return;
    zei->updateHandle = handle;
    if (zei->write_flag)
    {
	struct zebDatabaseInfoB *zdi;
	zebAccessObject o;

	/* write each database info record */
	for (zdi = zei->databaseInfo; zdi; zdi = zdi->next)
	{
	    zebraExplain_writeDatabase(zei, zdi, 1);
	    zebraExplain_writeAttributeDetails(zei, zdi->attributeDetails,
						zdi->databaseName, 1);
	}
	zebraExplain_writeTarget(zei, 1);
	zebraExplain_writeCategoryList(zei,
					zei->categoryList,
					1);
	assert(zei->accessInfo);
	for (o = zei->accessInfo->attributeSetIds; o; o = o->next)
	    if (!o->sysno)
		zebraExplain_writeAttributeSet(zei, o, 1);
	for (o = zei->accessInfo->schemas; o; o = o->next)
	    if (!o->sysno)
	    {
/* 		zebraExplain_writeSchema(zei, o, 1); */
	    }

	for (zdi = zei->databaseInfo; zdi; zdi = zdi->next)
	{
	    zebraExplain_writeDatabase(zei, zdi, 0);
	    zebraExplain_writeAttributeDetails(zei, zdi->attributeDetails,
						zdi->databaseName, 0);
	}
	zebraExplain_writeTarget(zei, 0);
    }
}

void zebraExplain_close(ZebraExplainInfo zei)
{
#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_close");
#endif
    if (!zei)
	return;
    zebraExplain_flush(zei, zei->updateHandle);
    nmem_destroy(zei->nmem);
}

void zebraExplain_mergeOids (ZebraExplainInfo zei, data1_node *n,
			     zebAccessObject *op)
{
    data1_node *np;

    for (np = n->child; np; np = np->next)
    {
	char str[64];
	int len;
	Odr_oid *oid;
	zebAccessObject ao;

	if (np->which != DATA1N_tag || strcmp(np->u.tag.tag, "oid"))
	    continue;
	len = np->child->u.data.len;
	if (len > 63)
	    len = 63;
	memcpy(str, np->child->u.data.data, len);
	str[len] = '\0';
	
	oid = odr_getoidbystr_nmem(zei->nmem, str);

	for (ao = *op; ao; ao = ao->next)
	    if (!oid_oidcmp(oid, ao->oid))
	    {
		ao->sysno = 1;
		break;
	    }
	if (!ao)
	{
	    ao = (zebAccessObject) nmem_malloc(zei->nmem, sizeof(*ao));
	    ao->handle = NULL;
	    ao->sysno = 1;
	    ao->oid = oid;
	    ao->next = *op;
	    *op = ao;
	}
    }
}

void zebraExplain_mergeAccessInfo(ZebraExplainInfo zei, data1_node *n,
				   zebAccessInfo *accessInfo)
{
    data1_node *np;
    
    if (!n)
    {
	*accessInfo = (zebAccessInfo)
	    nmem_malloc(zei->nmem, sizeof(**accessInfo));
	(*accessInfo)->attributeSetIds = NULL;
	(*accessInfo)->schemas = NULL;
    }
    else
    {
	if (!(n = data1_search_tag(zei->dh, n->child, "accessInfo")))
	    return;
	if ((np = data1_search_tag(zei->dh, n->child, "attributeSetIds")))
	    zebraExplain_mergeOids(zei, np,
				    &(*accessInfo)->attributeSetIds);
	if ((np = data1_search_tag(zei->dh, n->child, "schemas")))
	    zebraExplain_mergeOids(zei, np,
				    &(*accessInfo)->schemas);
    }
}

/* Explain structure
    root record
      of type targetInfo
      and has sysno = 1

    databaseList (list of databases)
*/
/*
Example root:
explain:
  targetInfo: TargetInfo
    name: Zebra
    namedResultSets: 1
    multipleDbSearch: 1
    nicknames:
      name: Zebra
    commonInfo:
      dateAdded: 20030630190601
      dateChanged: 20030630190601
      languageCode: EN
    accessinfo:
      unitSystems:
        string: ISO
      attributeSetIds:
        oid: 1.2.840.10003.3.2
        oid: 1.2.840.10003.3.5
        oid: 1.2.840.10003.3.1
      schemas:
        oid: 1.2.840.10003.13.1000.81.2
        oid: 1.2.840.10003.13.2
    zebraInfo:
      version: 1.3.12
      databaseList:
        database:
          name: Default
          id: 50
          attributeDetailsId: 51
        database:
          name: IR-Explain-1
          id: 52
          attributeDetailsId: 53
      ordinalSU: 38
      runNumber: 1
nextResultSetPosition = 2
*/

ZebraExplainInfo zebraExplain_open(
    Records records, data1_handle dh,
    Res res,
    int writeFlag,
    void *updateHandle,
    int (*updateFunc)(void *handle, Record drec, data1_node *n))
{
    Record trec;
    ZebraExplainInfo zei;
    struct zebDatabaseInfoB **zdip;
    time_t our_time;
    struct tm *tm;
    NMEM nmem = nmem_create();

#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_open wr=%d", writeFlag);
#endif
    zei = (ZebraExplainInfo) nmem_malloc(nmem, sizeof(*zei));
    zei->databaseInfo = 0;
    zei->write_flag = writeFlag;
    zei->updateHandle = updateHandle;
    zei->updateFunc = updateFunc;
    zei->dirty = 0;
    zei->ordinalDatabase = 1;
    zei->curDatabaseInfo = NULL;
    zei->records = records;
    zei->nmem = nmem;
    zei->dh = dh;
    zei->attsets = NULL;
    zei->res = res;
    zei->categoryList = (struct zebraCategoryListInfo *)
	nmem_malloc(zei->nmem, sizeof(*zei->categoryList));
    zei->categoryList->sysno = 0;
    zei->categoryList->dirty = 0;
    zei->categoryList->data1_categoryList = NULL;

    if ( atoi(res_get_def(res, "notimestamps", "0") )== 0)
    {
        time(&our_time);
        tm = localtime(&our_time);
        sprintf(zei->date, "%04d%02d%02d%02d%02d%02d",
	         tm->tm_year+1900, tm->tm_mon+1,  tm->tm_mday,
	         tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        sprintf(zei->date, "%04d%02d%02d%02d%02d%02d",
	         0, 0, 0,  0, 0, 0);
    }
    zdip = &zei->databaseInfo;
    trec = rec_get_root(records);      /* get "root" record */

    zei->ordinalSU = 1;
    zei->runNumber = 0;

    zebraExplain_mergeAccessInfo(zei, 0, &zei->accessInfo);
    if (trec)    /* targetInfo already exists ... */
    {
	data1_node *node_tgtinfo, *node_zebra, *node_list, *np;

	zei->data1_target = read_sgml_rec(zei->dh, zei->nmem, trec);
#if 0
	if (!zei->data1_target || !zei->data1_target->u.root.absyn)
#else
	if (!zei->data1_target)
#endif
	{
	    yaz_log(YLOG_FATAL, "Explain schema missing. Check profilePath");
	    nmem_destroy(zei->nmem);
	    return 0;
	}
#if ZINFO_DEBUG
	data1_pr_tree(zei->dh, zei->data1_target, stderr);
#endif
	node_tgtinfo = data1_search_tag(zei->dh, zei->data1_target,
					 "/targetInfo");
	zebraExplain_mergeAccessInfo(zei, node_tgtinfo,
				      &zei->accessInfo);

	node_zebra = data1_search_tag(zei->dh, node_tgtinfo->child,
				       "zebraInfo");
	np = 0;
	if (node_zebra)
	{
	    node_list = data1_search_tag(zei->dh, node_zebra->child,
					  "databaseList");
	    if (node_list)
		np = node_list->child;
	}
	for(; np; np = np->next)
	{
	    data1_node *node_name = NULL;
	    data1_node *node_id = NULL;
	    data1_node *node_aid = NULL;
	    data1_node *np2;
	    if (np->which != DATA1N_tag || strcmp(np->u.tag.tag, "database"))
		continue;
	    for(np2 = np->child; np2; np2 = np2->next)
	    {
		if (np2->which != DATA1N_tag)
		    continue;
		if (!strcmp(np2->u.tag.tag, "name"))
		    node_name = np2->child;
		else if (!strcmp(np2->u.tag.tag, "id"))
		    node_id = np2->child;
		else if (!strcmp(np2->u.tag.tag, "attributeDetailsId"))
		    node_aid = np2->child;
	    }
	    assert(node_id && node_name && node_aid);
	    
	    *zdip =(struct zebDatabaseInfoB *) 
		nmem_malloc(zei->nmem, sizeof(**zdip));
            (*zdip)->readFlag = 1;
            (*zdip)->dirty = 0;
	    (*zdip)->data1_database = NULL;
	    (*zdip)->recordCount = 0;
	    (*zdip)->recordBytes = 0;
	    zebraExplain_mergeAccessInfo (zei, 0, &(*zdip)->accessInfo);

	    (*zdip)->databaseName = (char *)
		nmem_malloc (zei->nmem, 1+node_name->u.data.len);
	    memcpy((*zdip)->databaseName, node_name->u.data.data,
		   node_name->u.data.len);
	    (*zdip)->databaseName[node_name->u.data.len] = '\0';
	    (*zdip)->sysno = atoi_zn (node_id->u.data.data,
				      node_id->u.data.len);
	    (*zdip)->attributeDetails = (zebAttributeDetails)
		nmem_malloc (zei->nmem, sizeof(*(*zdip)->attributeDetails));
	    (*zdip)->attributeDetails->sysno = atoi_zn (node_aid->u.data.data,
							node_aid->u.data.len);
	    (*zdip)->attributeDetails->readFlag = 1;
	    (*zdip)->attributeDetails->dirty = 0;
	    (*zdip)->attributeDetails->SUInfo = NULL;

	    zdip = &(*zdip)->next;
	}
	if (node_zebra)
	{
	    np = data1_search_tag(zei->dh, node_zebra->child,
				  "ordinalSU");
	    np = np->child;
	    assert (np && np->which == DATA1N_data);
	    zei->ordinalSU = atoi_n(np->u.data.data, np->u.data.len);
	    
	    np = data1_search_tag(zei->dh, node_zebra->child,
				  "ordinalDatabase");
	    np = np->child;
	    assert (np && np->which == DATA1N_data);
	    zei->ordinalDatabase = atoi_n(np->u.data.data, np->u.data.len);

	    np = data1_search_tag(zei->dh, node_zebra->child,
				   "runNumber");
	    np = np->child;
	    assert (np && np->which == DATA1N_data);
	    zei->runNumber = atoi_zn(np->u.data.data, np->u.data.len);
            yaz_log(YLOG_DEBUG, "read runnumber=" ZINT_FORMAT, zei->runNumber);
	    *zdip = NULL;
	}
	rec_rm(&trec);
    }
    else  /* create initial targetInfo */
    {
	data1_node *node_tgtinfo;

	*zdip = NULL;
	if (writeFlag)
	{
	    char *sgml_buf;
	    int sgml_len;

	    zei->data1_target =
		data1_read_sgml(zei->dh, zei->nmem,
				 "<explain><targetInfo>TargetInfo\n"
				 "<name>Zebra</>\n"
				 "<namedResultSets>1</>\n"
				 "<multipleDBSearch>1</>\n"
				 "<nicknames><name>Zebra</></>\n"
				 "</></>\n" );
	    if (!zei->data1_target)
	    {
		yaz_log(YLOG_FATAL, "Explain schema missing. Check profilePath");
		nmem_destroy(zei->nmem);
		return 0;
	    }
	    node_tgtinfo = data1_search_tag(zei->dh, zei->data1_target,
                                             "/targetInfo");
	    assert(node_tgtinfo);

	    zebraExplain_initCommonInfo(zei, node_tgtinfo);
	    zebraExplain_initAccessInfo(zei, node_tgtinfo);

	    /* write now because we want to be sure about the sysno */
	    trec = rec_new(records);
	    trec->info[recInfo_fileType] =
		rec_strdup("grs.sgml", &trec->size[recInfo_fileType]);
	    trec->info[recInfo_databaseName] =
		rec_strdup("IR-Explain-1", &trec->size[recInfo_databaseName]);
	    
	    sgml_buf = data1_nodetoidsgml(dh, zei->data1_target, 0, &sgml_len);
	    trec->info[recInfo_storeData] = (char *) xmalloc(sgml_len);
	    memcpy(trec->info[recInfo_storeData], sgml_buf, sgml_len);
	    trec->size[recInfo_storeData] = sgml_len;
	    
	    rec_put(records, &trec);
	    rec_rm(&trec);

	}
	zebraExplain_newDatabase(zei, "IR-Explain-1", 0);
	    
	if (!zei->categoryList->dirty)
	{
	    struct zebraCategoryListInfo *zcl = zei->categoryList;
	    data1_node *node_cl;
	    
	    zcl->dirty = 1;
	    zcl->data1_categoryList =
		data1_read_sgml(zei->dh, zei->nmem,
				 "<explain><categoryList>CategoryList\n"
				 "</></>\n");
	
	    if (zcl->data1_categoryList)
	    {
		node_cl = data1_search_tag(zei->dh, zcl->data1_categoryList,
					    "/categoryList");
		assert(node_cl);
		zebraExplain_initCommonInfo(zei, node_cl);
	    }
	}
    }
    return zei;
}

static void zebraExplain_readAttributeDetails(ZebraExplainInfo zei,
					       zebAttributeDetails zad)
{
    Record rec;
    struct zebSUInfoB **zsuip = &zad->SUInfo;
    data1_node *node_adinfo, *node_zebra, *node_list, *np;

    assert (zad->sysno);
    rec = rec_get(zei->records, zad->sysno);

    zad->data1_tree = read_sgml_rec(zei->dh, zei->nmem, rec);

    node_adinfo = data1_search_tag(zei->dh, zad->data1_tree,
				    "/attributeDetails");
    node_zebra = data1_search_tag(zei->dh, node_adinfo->child,
				 "zebraInfo");
    node_list = data1_search_tag(zei->dh, node_zebra->child,
				  "attrlist");
    for (np = node_list->child; np; np = np->next)
    {
	data1_node *node_set = NULL;
	data1_node *node_use = NULL;
	data1_node *node_str = NULL;
	data1_node *node_ordinal = NULL;
	data1_node *node_type = NULL;
	data1_node *np2;
	char oid_str[128];
	int oid_str_len;

	if (np->which != DATA1N_tag || strcmp(np->u.tag.tag, "attr"))
	    continue;
	for (np2 = np->child; np2; np2 = np2->next)
	{
	    if (np2->which != DATA1N_tag || !np2->child ||
		np2->child->which != DATA1N_data)
		continue;
	    if (!strcmp(np2->u.tag.tag, "set"))
		node_set = np2->child;
	    else if (!strcmp(np2->u.tag.tag, "use"))
		node_use = np2->child;
	    else if (!strcmp(np2->u.tag.tag, "str"))
		node_str = np2->child;
	    else if (!strcmp(np2->u.tag.tag, "ordinal"))
		node_ordinal = np2->child;
	    else if (!strcmp(np2->u.tag.tag, "type"))
		node_type = np2->child;
	}
	assert(node_ordinal);

        *zsuip = (struct zebSUInfoB *)
	    nmem_malloc(zei->nmem, sizeof(**zsuip));

	if (node_type && node_type->u.data.len > 0)
	    (*zsuip)->info.index_type =  node_type->u.data.data[0];
	else
	{
	    yaz_log(YLOG_WARN, "Missing attribute 'type' in attribute info");
	    (*zsuip)->info.index_type = 'w';
	}

	if (node_set && node_use)
	{
	    (*zsuip)->info.which = ZEB_SU_SET_USE;
	    
	    oid_str_len = node_set->u.data.len;
	    if (oid_str_len >= (int) sizeof(oid_str))
		oid_str_len = sizeof(oid_str)-1;
	    memcpy(oid_str, node_set->u.data.data, oid_str_len);
	    oid_str[oid_str_len] = '\0';

	    (*zsuip)->info.u.su.set = oid_getvalbyname(oid_str);
	    
	    (*zsuip)->info.u.su.use = atoi_n(node_use->u.data.data,
					 node_use->u.data.len);
	    yaz_log(YLOG_DEBUG, "set=%d use=%d ordinal=%d",
		     (*zsuip)->info.u.su.set, (*zsuip)->info.u.su.use,
		     (*zsuip)->info.ordinal);
	}
	else if (node_str)
	{
	    (*zsuip)->info.which = ZEB_SU_STR;
	    
	    (*zsuip)->info.u.str = nmem_strdupn(zei->nmem,
						node_str->u.data.data,
						node_str->u.data.len);
	}
	else
	{
	    yaz_log(YLOG_WARN, "Missing set/use/str in attribute info");
	    continue;
	}
	(*zsuip)->info.ordinal = atoi_n (node_ordinal->u.data.data,
					 node_ordinal->u.data.len);
        zsuip = &(*zsuip)->next;
    }
    *zsuip = NULL;
    zad->readFlag = 0;
    rec_rm (&rec);
}

static void zebraExplain_readDatabase (ZebraExplainInfo zei,
				       struct zebDatabaseInfoB *zdi)
{
    Record rec;
    data1_node *node_dbinfo, *node_zebra, *np;

    assert (zdi->sysno);
    rec = rec_get (zei->records, zdi->sysno);

    zdi->data1_database = read_sgml_rec (zei->dh, zei->nmem, rec);
    
    node_dbinfo = data1_search_tag (zei->dh, zdi->data1_database,
                                    "/databaseInfo");
    assert (node_dbinfo);
    zebraExplain_mergeAccessInfo (zei, node_dbinfo, &zdi->accessInfo);

    node_zebra = data1_search_tag (zei->dh, node_dbinfo->child,
				 "zebraInfo");
    if (node_zebra
	&& (np = data1_search_tag (zei->dh, node_zebra->child,
				   "recordBytes")) 
	&& np->child && np->child->which == DATA1N_data)
	zdi->recordBytes = atoi_zn (np->child->u.data.data,
				    np->child->u.data.len);

    if (node_zebra
	&& (np = data1_search_tag (zei->dh, node_zebra->child,
				   "ordinalDatabase")) 
	&& np->child && np->child->which == DATA1N_data)
	zdi->ordinalDatabase = atoi_n(np->child->u.data.data,
				      np->child->u.data.len);

    if ((np = data1_search_tag (zei->dh, node_dbinfo->child,
				"recordCount")) &&
	(np = data1_search_tag (zei->dh, np->child,
				"recordCountActual")) &&
	np->child->which == DATA1N_data)
    {
	zdi->recordCount = atoi_zn (np->child->u.data.data,
				    np->child->u.data.len);
    }
    zdi->readFlag = 0;
    rec_rm (&rec);
}

int zebraExplain_removeDatabase(ZebraExplainInfo zei, void *update_handle)
{
    struct zebDatabaseInfoB **zdip = &zei->databaseInfo;

    while (*zdip)
    {
	if (*zdip == zei->curDatabaseInfo)
	{
	    struct zebDatabaseInfoB *zdi = *zdip;
	    Record rec;

	    zei->dirty = 1;
	    zei->updateHandle = update_handle;

	    if (zdi->attributeDetails)
	    {
		/* remove attribute details keys and delete it */
		zebAttributeDetails zad = zdi->attributeDetails;
		
		rec = rec_get(zei->records, zad->sysno);
		(*zei->updateFunc)(zei->updateHandle, rec, 0);
		rec_rm(&rec);
	    }
	    /* remove database record keys and delete it */
	    rec = rec_get (zei->records, zdi->sysno);
	    (*zei->updateFunc)(zei->updateHandle, rec, 0);
	    rec_rm(&rec);

	    /* remove from list */
	    *zdip = zdi->next;

	    /* current database is IR-Explain-1 */
	    return 0;
	}
	zdip = &(*zdip)->next;
    }
    return -1;
}

int zebraExplain_curDatabase (ZebraExplainInfo zei, const char *database)
{
    struct zebDatabaseInfoB *zdi;
    const char *database_n = strrchr (database, '/');

    if (database_n)
        database_n++;
    else
        database_n = database;
    
    assert (zei);
    if (zei->curDatabaseInfo &&
        !STRCASECMP (zei->curDatabaseInfo->databaseName, database))
        return 0;
    for (zdi = zei->databaseInfo; zdi; zdi=zdi->next)
    {
        if (!STRCASECMP (zdi->databaseName, database_n))
            break;
    }
    if (!zdi)
        return -1;
#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_curDatabase: %s", database);
#endif
    if (zdi->readFlag)
    {
#if ZINFO_DEBUG
	yaz_log(YLOG_LOG, "zebraExplain_readDatabase: %s", database);
#endif
        zebraExplain_readDatabase (zei, zdi);
    }
    if (zdi->attributeDetails->readFlag)
    {
#if ZINFO_DEBUG
	yaz_log(YLOG_LOG, "zebraExplain_readAttributeDetails: %s", database);
#endif
        zebraExplain_readAttributeDetails (zei, zdi->attributeDetails);
    }
    zei->curDatabaseInfo = zdi;
    return 0;
}

static void zebraExplain_initCommonInfo (ZebraExplainInfo zei, data1_node *n)
{
    data1_node *c = data1_mk_tag (zei->dh, zei->nmem, "commonInfo", 0, n);
    data1_mk_tag_data_text (zei->dh, c, "dateAdded", zei->date, zei->nmem);
    data1_mk_tag_data_text (zei->dh, c, "dateChanged", zei->date, zei->nmem);
    data1_mk_tag_data_text (zei->dh, c, "languageCode", "EN", zei->nmem);
}

static void zebraExplain_updateCommonInfo (ZebraExplainInfo zei, data1_node *n)
{
    data1_node *c = data1_search_tag (zei->dh, n->child, "commonInfo");
    assert (c);
    data1_mk_tag_data_text_uni (zei->dh, c, "dateChanged", zei->date,
                                zei->nmem);
}

static void zebraExplain_initAccessInfo (ZebraExplainInfo zei, data1_node *n)
{
    data1_node *c = data1_mk_tag (zei->dh, zei->nmem, "accessInfo", 0, n);
    data1_node *d = data1_mk_tag (zei->dh, zei->nmem, "unitSystems", 0, c);
    data1_mk_tag_data_text (zei->dh, d, "string", "ISO", zei->nmem);
}

static void zebraExplain_updateAccessInfo (ZebraExplainInfo zei, data1_node *n,
					   zebAccessInfo accessInfo)
{
    data1_node *c = data1_search_tag (zei->dh, n->child, "accessInfo");
    data1_node *d;
    zebAccessObject p;
    
    if (!c)
    {
        data1_pr_tree (zei->dh, n, stdout);
        exit (0);
        assert (c);
    }

    if ((p = accessInfo->attributeSetIds))
    {
	d = data1_mk_tag_uni (zei->dh, zei->nmem, "attributeSetIds", c);
	for (; p; p = p->next)
	    data1_mk_tag_data_oid (zei->dh, d, "oid", p->oid, zei->nmem);
    }
    if ((p = accessInfo->schemas))
    {
	d = data1_mk_tag_uni (zei->dh, zei->nmem, "schemas", c);
	for (; p; p = p->next)
	    data1_mk_tag_data_oid (zei->dh, d, "oid", p->oid, zei->nmem);
    }
}

int zebraExplain_newDatabase (ZebraExplainInfo zei, const char *database,
			      int explain_database)
{
    struct zebDatabaseInfoB *zdi;
    data1_node *node_dbinfo, *node_adinfo;
    const char *database_n = strrchr (database, '/');

    if (database_n)
        database_n++;
    else
        database_n = database;

#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_newDatabase: %s", database);
#endif
    assert (zei);
    for (zdi = zei->databaseInfo; zdi; zdi=zdi->next)
    {
        if (!STRCASECMP (zdi->databaseName, database_n))
            break;
    }
    if (zdi)
        return -1;
    /* it's new really. make it */
    zdi = (struct zebDatabaseInfoB *) nmem_malloc (zei->nmem, sizeof(*zdi));
    zdi->next = zei->databaseInfo;
    zei->databaseInfo = zdi;
    zdi->sysno = 0;
    zdi->recordCount = 0;
    zdi->recordBytes = 0;
    zdi->readFlag = 0;
    zdi->databaseName = nmem_strdup (zei->nmem, database_n);

    zdi->ordinalDatabase = zei->ordinalDatabase++;

    zebraExplain_mergeAccessInfo (zei, 0, &zdi->accessInfo);
    
    assert (zei->dh);
    assert (zei->nmem);

    zdi->data1_database =
	data1_read_sgml (zei->dh, zei->nmem, 
			 "<explain><databaseInfo>DatabaseInfo\n"
			 "</></>\n");
    if (!zdi->data1_database)
	return -2;

    node_dbinfo = data1_search_tag (zei->dh, zdi->data1_database,
                                    "/databaseInfo");
    assert (node_dbinfo);

    zebraExplain_initCommonInfo (zei, node_dbinfo);
    zebraExplain_initAccessInfo (zei, node_dbinfo);

    data1_mk_tag_data_text (zei->dh, node_dbinfo, "name",
			       database, zei->nmem);
    
    if (explain_database)
	data1_mk_tag_data_text (zei->dh, node_dbinfo, "explainDatabase",
				"", zei->nmem);
    
    data1_mk_tag_data_text (zei->dh, node_dbinfo, "userFee",
			    "0", zei->nmem);
    
    data1_mk_tag_data_text (zei->dh, node_dbinfo, "available",
			    "1", zei->nmem);
    
#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, zdi->data1_database, stderr);
#endif
    zdi->dirty = 1;
    zei->dirty = 1;
    zei->curDatabaseInfo = zdi;

    zdi->attributeDetails = (zebAttributeDetails)
	nmem_malloc (zei->nmem, sizeof(*zdi->attributeDetails));
    zdi->attributeDetails->readFlag = 0;
    zdi->attributeDetails->sysno = 0;
    zdi->attributeDetails->dirty = 1;
    zdi->attributeDetails->SUInfo = NULL;
    zdi->attributeDetails->data1_tree =
	data1_read_sgml (zei->dh, zei->nmem,
			 "<explain><attributeDetails>AttributeDetails\n"
			 "</></>\n");

    node_adinfo = data1_search_tag (zei->dh, zdi->attributeDetails->data1_tree,
                                    "/attributeDetails");
    assert (node_adinfo);

    zebraExplain_initCommonInfo (zei, node_adinfo);

    return 0;
}

static void writeAttributeValueDetails (ZebraExplainInfo zei,
				  zebAttributeDetails zad,
				  data1_node *node_atvs, data1_attset *attset)

{
    struct zebSUInfoB *zsui;
    int set_ordinal = attset->reference;
    data1_attset_child *c;

    for (c = attset->children; c; c = c->next)
	writeAttributeValueDetails (zei, zad, node_atvs, c->child);
    for (zsui = zad->SUInfo; zsui; zsui = zsui->next)
    {
	if (zsui->info.which == ZEB_SU_SET_USE && 
	    set_ordinal == zsui->info.u.su.set)
	{
	    data1_node *node_attvalue, *node_value;
	    node_attvalue = data1_mk_tag (zei->dh, zei->nmem, "attributeValue",
					  0 /* attr */, node_atvs);
	    node_value = data1_mk_tag (zei->dh, zei->nmem, "value",
				       0 /* attr */, node_attvalue);
	    data1_mk_tag_data_int (zei->dh, node_value, "numeric",
				   zsui->info.u.su.use, zei->nmem);
	}
    }
}

static void zebraExplain_writeCategoryList (ZebraExplainInfo zei,
					    struct zebraCategoryListInfo *zcl,
					    int key_flush)
{
    char *sgml_buf;
    int sgml_len;
    int i;
    Record drec;
    data1_node *node_ci, *node_categoryList;
    SYSNO sysno = 0;
    static char *category[] = {
	"CategoryList",
	"TargetInfo",
	"DatabaseInfo",
	"AttributeDetails",
	NULL
    };

    assert (zcl);
    if (!zcl->dirty)
	return ;
    zcl->dirty = 1;
    node_categoryList = zcl->data1_categoryList;

#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_writeCategoryList");
#endif

    drec = createRecord (zei->records, &sysno);
    
    node_ci = data1_search_tag (zei->dh, node_categoryList,
				"/categoryList");
    assert (node_ci);
    node_ci = data1_mk_tag (zei->dh, zei->nmem, "categories", 0 /* attr */,
                            node_ci);
    assert (node_ci);
    
    for (i = 0; category[i]; i++)
    {
	data1_node *node_cat = data1_mk_tag (zei->dh, zei->nmem,  "category",
                                             0 /* attr */, node_ci);

	data1_mk_tag_data_text (zei->dh, node_cat, "name",
				category[i], zei->nmem);
    }
    /* extract *searchable* keys from it. We do this here, because
       record count, etc. is affected */
    if (key_flush)
	(*zei->updateFunc)(zei->updateHandle, drec, node_categoryList);

    /* convert to "SGML" and write it */
#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, node_categoryList, stderr);
#endif
    sgml_buf = data1_nodetoidsgml(zei->dh, node_categoryList, 0, &sgml_len);
    drec->info[recInfo_storeData] = (char *) xmalloc (sgml_len);
    memcpy (drec->info[recInfo_storeData], sgml_buf, sgml_len);
    drec->size[recInfo_storeData] = sgml_len;
    
    rec_put (zei->records, &drec);
}

static void zebraExplain_writeAttributeDetails (ZebraExplainInfo zei,
						zebAttributeDetails zad,
						const char *databaseName,
						int key_flush)
{
    char *sgml_buf;
    int sgml_len;
    Record drec;
    data1_node *node_adinfo, *node_list, *node_zebra, *node_attributesBySet;
    struct zebSUInfoB *zsui;
    int set_min;
    
    if (!zad->dirty)
	return;
    
    zad->dirty = 0;
#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_writeAttributeDetails");    
#endif

    drec = createRecord (zei->records, &zad->sysno);
    assert (zad->data1_tree);

    node_adinfo = data1_search_tag (zei->dh, zad->data1_tree,
				   "/attributeDetails");
    zebraExplain_updateCommonInfo (zei, node_adinfo);

    data1_mk_tag_data_text (zei->dh, node_adinfo, "name",
			    databaseName, zei->nmem);

    /* extract *searchable* keys from it. We do this here, because
       record count, etc. is affected */
    if (key_flush)
	(*zei->updateFunc)(zei->updateHandle, drec, zad->data1_tree);

    node_attributesBySet = data1_mk_tag_uni (zei->dh, zei->nmem,
					   "attributesBySet", node_adinfo);
    set_min = -1;
    while (1)
    {
	data1_node *node_asd;
	data1_attset *attset;
	int set_ordinal = -1;
	for (zsui = zad->SUInfo; zsui; zsui = zsui->next)
	{
	    if (zsui->info.which == ZEB_SU_SET_USE &&
		(set_ordinal < 0 || set_ordinal > zsui->info.u.su.set)
		&& zsui->info.u.su.set > set_min)
		set_ordinal = zsui->info.u.su.set;
	}
	if (set_ordinal < 0)
	    break;
	set_min = set_ordinal;
	node_asd = data1_mk_tag (zei->dh, zei->nmem,
                                 "attributeSetDetails",
                                 0 /* attr */, node_attributesBySet);

	attset = data1_attset_search_id (zei->dh, set_ordinal);
	if (!attset)
	{
	    zebraExplain_loadAttsets (zei->dh, zei->res);
	    attset = data1_attset_search_id (zei->dh, set_ordinal);
	}
	if (attset)
	{
	    int oid[OID_SIZE];
	    oident oe;
	    
	    oe.proto = PROTO_Z3950;
	    oe.oclass = CLASS_ATTSET;
	    oe.value = (enum oid_value) set_ordinal;
	    
	    if (oid_ent_to_oid (&oe, oid))
	    {
		data1_node *node_abt, *node_atd, *node_atvs;
		data1_mk_tag_data_oid (zei->dh, node_asd, "oid",
				       oid, zei->nmem);
		
		node_abt = data1_mk_tag (zei->dh, zei->nmem,
                                         "attributesByType",
                                         0 /*attr */, node_asd);
		node_atd = data1_mk_tag (zei->dh, zei->nmem,
                                         "attributeTypeDetails", 
                                         0 /* attr */, node_abt);
		data1_mk_tag_data_int (zei->dh, node_atd,
				       "type", 1, zei->nmem);
		node_atvs = data1_mk_tag (zei->dh, zei->nmem, 
                                          "attributeValues",
                                          0 /* attr */, node_atd);
		writeAttributeValueDetails (zei, zad, node_atvs, attset);
	    }
	}
    }
    /* zebra info (private) */
    node_zebra = data1_mk_tag_uni (zei->dh, zei->nmem,
				 "zebraInfo", node_adinfo);
    node_list = data1_mk_tag_uni (zei->dh, zei->nmem,
				 "attrlist", node_zebra);
    for (zsui = zad->SUInfo; zsui; zsui = zsui->next)
    {
	struct oident oident;
	int oid[OID_SIZE];
	data1_node *node_attr;
	char index_type_str[2];

	
	node_attr = data1_mk_tag (zei->dh, zei->nmem, "attr", 0 /* attr */,
                                  node_list);

	index_type_str[0] = zsui->info.index_type;
	index_type_str[1] = '\0';
	data1_mk_tag_data_text (zei->dh, node_attr, "type",
				index_type_str, zei->nmem);
	if (zsui->info.which == ZEB_SU_SET_USE)
	{
	    oident.proto = PROTO_Z3950;
	    oident.oclass = CLASS_ATTSET;
	    oident.value = (enum oid_value) zsui->info.u.su.set;
	    oid_ent_to_oid (&oident, oid);
	    
	    data1_mk_tag_data_text (zei->dh, node_attr, "set",
				    oident.desc, zei->nmem);
	    data1_mk_tag_data_int (zei->dh, node_attr, "use",
				   zsui->info.u.su.use, zei->nmem);
	}
	else if (zsui->info.which == ZEB_SU_STR)
	{
	    data1_mk_tag_data_text (zei->dh, node_attr, "str",
				    zsui->info.u.str, zei->nmem);
	}
	data1_mk_tag_data_int (zei->dh, node_attr, "ordinal",
			       zsui->info.ordinal, zei->nmem);
    }
    /* convert to "SGML" and write it */
#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, zad->data1_tree, stderr);
#endif
    sgml_buf = data1_nodetoidsgml(zei->dh, zad->data1_tree,
				  0, &sgml_len);
    drec->info[recInfo_storeData] = (char *) xmalloc (sgml_len);
    memcpy (drec->info[recInfo_storeData], sgml_buf, sgml_len);
    drec->size[recInfo_storeData] = sgml_len;
    
    rec_put (zei->records, &drec);
}

static void zebraExplain_writeDatabase (ZebraExplainInfo zei,
                                        struct zebDatabaseInfoB *zdi,
					int key_flush)
{
    char *sgml_buf;
    int sgml_len;
    Record drec;
    data1_node *node_dbinfo, *node_count, *node_zebra;
    
    if (!zdi->dirty)
	return;

    zdi->dirty = 0;
#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_writeDatabase %s", zdi->databaseName);
#endif
    drec = createRecord (zei->records, &zdi->sysno);
    assert (zdi->data1_database);

    node_dbinfo = data1_search_tag (zei->dh, zdi->data1_database,
                                    "/databaseInfo");

    assert (node_dbinfo);
    zebraExplain_updateCommonInfo (zei, node_dbinfo);
    zebraExplain_updateAccessInfo (zei, node_dbinfo, zdi->accessInfo);

    /* extract *searchable* keys from it. We do this here, because
       record count, etc. is affected */
    if (key_flush)
	(*zei->updateFunc)(zei->updateHandle, drec, zdi->data1_database);
    /* record count */
    node_count = data1_mk_tag_uni (zei->dh, zei->nmem,
				 "recordCount", node_dbinfo);
    data1_mk_tag_data_zint (zei->dh, node_count, "recordCountActual",
			    zdi->recordCount, zei->nmem);

    /* zebra info (private) */
    node_zebra = data1_mk_tag_uni (zei->dh, zei->nmem,
				 "zebraInfo", node_dbinfo);
    data1_mk_tag_data_zint (zei->dh, node_zebra,
			   "recordBytes", zdi->recordBytes, zei->nmem);

    data1_mk_tag_data_zint(zei->dh, node_zebra,
			   "ordinalDatabase", zdi->ordinalDatabase, zei->nmem);

    /* convert to "SGML" and write it */
#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, zdi->data1_database, stderr);
#endif
    sgml_buf = data1_nodetoidsgml(zei->dh, zdi->data1_database,
				  0, &sgml_len);
    drec->info[recInfo_storeData] = (char *) xmalloc (sgml_len);
    memcpy (drec->info[recInfo_storeData], sgml_buf, sgml_len);
    drec->size[recInfo_storeData] = sgml_len;
    
    rec_put (zei->records, &drec);
}

static void writeAttributeValues (ZebraExplainInfo zei,
				  data1_node *node_values,
				  data1_attset *attset)
{
    data1_att *atts;
    data1_attset_child *c;

    if (!attset)
	return;

    for (c = attset->children; c; c = c->next)
	writeAttributeValues (zei, node_values, c->child);
    for (atts = attset->atts; atts; atts = atts->next)
    {
	data1_node *node_value;
	
	node_value = data1_mk_tag (zei->dh, zei->nmem, "attributeValue",
                                   0 /* attr */, node_values);
	data1_mk_tag_data_text (zei->dh, node_value, "name",
				atts->name, zei->nmem);
        node_value = data1_mk_tag (zei->dh, zei->nmem, "value",
                                   0 /* attr */, node_value);
	data1_mk_tag_data_int (zei->dh, node_value, "numeric",
			       atts->value, zei->nmem);
    }
}


static void zebraExplain_writeAttributeSet (ZebraExplainInfo zei,
					    zebAccessObject o,
					    int key_flush)
{
    char *sgml_buf;
    int sgml_len;
    Record drec;
    data1_node *node_root, *node_attinfo, *node_attributes, *node_atttype;
    data1_node *node_values;
    struct oident *entp;
    struct data1_attset *attset = NULL;
    
    if ((entp = oid_getentbyoid (o->oid)))
	attset = data1_attset_search_id (zei->dh, entp->value);
	    
#if ZINFO_DEBUG
    yaz_log(YLOG_LOG, "zebraExplain_writeAttributeSet %s",
	  attset ? attset->name : "<unknown>");    
#endif

    drec = createRecord (zei->records, &o->sysno);
    node_root =
	data1_read_sgml (zei->dh, zei->nmem,
			 "<explain><attributeSetInfo>AttributeSetInfo\n"
			 "</></>\n" );

    node_attinfo = data1_search_tag (zei->dh, node_root,
				   "/attributeSetInfo");

    assert (node_attinfo);
    zebraExplain_initCommonInfo (zei, node_attinfo);
    zebraExplain_updateCommonInfo (zei, node_attinfo);

    data1_mk_tag_data_oid (zei->dh, node_attinfo,
			    "oid", o->oid, zei->nmem);
    if (attset && attset->name)
	data1_mk_tag_data_text (zei->dh, node_attinfo,
				"name", attset->name, zei->nmem);
    
    node_attributes = data1_mk_tag_uni (zei->dh, zei->nmem,
				      "attributes", node_attinfo);
    node_atttype = data1_mk_tag_uni (zei->dh, zei->nmem,
				   "attributeType", node_attributes);
    data1_mk_tag_data_text (zei->dh, node_atttype,
			    "name", "Use", zei->nmem);
    data1_mk_tag_data_text (zei->dh, node_atttype,
			    "description", "Use Attribute", zei->nmem);
    data1_mk_tag_data_int (zei->dh, node_atttype,
			   "type", 1, zei->nmem);
    node_values = data1_mk_tag (zei->dh, zei->nmem,
                                "attributeValues", 0 /* attr */, node_atttype);
    if (attset)
	writeAttributeValues (zei, node_values, attset);

    /* extract *searchable* keys from it. We do this here, because
       record count, etc. is affected */
    if (key_flush)
	(*zei->updateFunc)(zei->updateHandle, drec, node_root);
    /* convert to "SGML" and write it */
#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, node_root, stderr);
#endif
    sgml_buf = data1_nodetoidsgml(zei->dh, node_root, 0, &sgml_len);
    drec->info[recInfo_storeData] = (char *) xmalloc (sgml_len);
    memcpy (drec->info[recInfo_storeData], sgml_buf, sgml_len);
    drec->size[recInfo_storeData] = sgml_len;
    
    rec_put (zei->records, &drec);
}

static void zebraExplain_writeTarget (ZebraExplainInfo zei, int key_flush)
{
    struct zebDatabaseInfoB *zdi;
    data1_node *node_tgtinfo, *node_list, *node_zebra;
    Record trec;
    int sgml_len;
    char *sgml_buf;

    if (!zei->dirty)
	return;
    zei->dirty = 0;

    trec = rec_get_root(zei->records);
    xfree (trec->info[recInfo_storeData]);

    node_tgtinfo = data1_search_tag (zei->dh, zei->data1_target,
                                     "/targetInfo");
    assert (node_tgtinfo);

    zebraExplain_updateCommonInfo (zei, node_tgtinfo);
    zebraExplain_updateAccessInfo (zei, node_tgtinfo, zei->accessInfo);

    /* convert to "SGML" and write it */
    if (key_flush)
	(*zei->updateFunc)(zei->updateHandle, trec, zei->data1_target);

    node_zebra = data1_mk_tag_uni (zei->dh, zei->nmem,
				 "zebraInfo", node_tgtinfo);
    data1_mk_tag_data_text (zei->dh, node_zebra, "version",
			       ZEBRAVER, zei->nmem);
    node_list = data1_mk_tag (zei->dh, zei->nmem,
                              "databaseList", 0 /* attr */, node_zebra);
    for (zdi = zei->databaseInfo; zdi; zdi = zdi->next)
    {
	data1_node *node_db;
	node_db = data1_mk_tag (zei->dh, zei->nmem,
                                "database", 0 /* attr */, node_list);
	data1_mk_tag_data_text (zei->dh, node_db, "name",
                                zdi->databaseName, zei->nmem);
	data1_mk_tag_data_zint (zei->dh, node_db, "id",
				zdi->sysno, zei->nmem);
	data1_mk_tag_data_zint (zei->dh, node_db, "attributeDetailsId",
				zdi->attributeDetails->sysno, zei->nmem);
    }
    data1_mk_tag_data_int (zei->dh, node_zebra, "ordinalSU",
                           zei->ordinalSU, zei->nmem);

    data1_mk_tag_data_int (zei->dh, node_zebra, "ordinalDatabase",
                           zei->ordinalDatabase, zei->nmem);

    data1_mk_tag_data_zint (zei->dh, node_zebra, "runNumber",
			    zei->runNumber, zei->nmem);

#if ZINFO_DEBUG
    data1_pr_tree (zei->dh, zei->data1_target, stderr);
#endif
    sgml_buf = data1_nodetoidsgml(zei->dh, zei->data1_target,
				  0, &sgml_len);
    trec->info[recInfo_storeData] = (char *) xmalloc (sgml_len);
    memcpy (trec->info[recInfo_storeData], sgml_buf, sgml_len);
    trec->size[recInfo_storeData] = sgml_len;
    
    rec_put (zei->records, &trec);
}

int zebraExplain_lookup_attr_su_any_index(ZebraExplainInfo zei,
					  int set, int use)
{
    struct zebSUInfoB *zsui;

    assert (zei->curDatabaseInfo);
    for (zsui = zei->curDatabaseInfo->attributeDetails->SUInfo;
	 zsui; zsui=zsui->next)
        if (zsui->info.which == ZEB_SU_SET_USE &&
	    zsui->info.u.su.use == use && zsui->info.u.su.set == set)
            return zsui->info.ordinal;
    return -1;
}

int zebraExplain_lookup_attr_su(ZebraExplainInfo zei, int index_type,
				int set, int use)
{
    struct zebSUInfoB *zsui;

    assert (zei->curDatabaseInfo);
    for (zsui = zei->curDatabaseInfo->attributeDetails->SUInfo;
	 zsui; zsui=zsui->next)
        if (zsui->info.index_type == index_type &&
	    zsui->info.which == ZEB_SU_SET_USE &&
	    zsui->info.u.su.use == use && zsui->info.u.su.set == set)
            return zsui->info.ordinal;
    return -1;
}

int zebraExplain_lookup_attr_str(ZebraExplainInfo zei, int index_type,
				 const char *str)
{
    struct zebSUInfoB *zsui;

    assert (zei->curDatabaseInfo);
    for (zsui = zei->curDatabaseInfo->attributeDetails->SUInfo;
	 zsui; zsui=zsui->next)
        if (zsui->info.index_type == index_type &&
	    zsui->info.which == ZEB_SU_STR && !strcmp(zsui->info.u.str, str))
            return zsui->info.ordinal;
    return -1;
}

int zebraExplain_trav_ord(ZebraExplainInfo zei, void *handle,
			  int (*f)(void *handle, int ord))
{
    struct zebDatabaseInfoB *zdb = zei->curDatabaseInfo;
    if (zdb)
    {
	struct zebSUInfoB *zsui = zdb->attributeDetails->SUInfo;
	for ( ;zsui; zsui = zsui->next)
	    (*f)(handle,  zsui->info.ordinal);
    }
    return 0;
}
			  
int zebraExplain_lookup_ord (ZebraExplainInfo zei, int ord,
			     int *index_type, 
			     const char **db,
			     int *set, int *use)
{
    struct zebDatabaseInfoB *zdb;
    for (zdb = zei->databaseInfo; zdb; zdb = zdb->next)
    {
	struct zebSUInfoB *zsui = zdb->attributeDetails->SUInfo;
	for ( ;zsui; zsui = zsui->next)
	    if (zsui->info.ordinal == ord)
	    {
		if (db)
		    *db = zdb->databaseName;
		if (zsui->info.which == ZEB_SU_SET_USE)
		{
		    if (set)
			*set = zsui->info.u.su.set;
		    if (use)
			*use = zsui->info.u.su.use;
		}
		if (index_type)
		    *index_type = zsui->info.index_type;
		return 0;
	    }
    }
    return -1;
}

zebAccessObject zebraExplain_announceOid (ZebraExplainInfo zei,
					  zebAccessObject *op,
					  Odr_oid *oid)
{
    zebAccessObject ao;
    
    for (ao = *op; ao; ao = ao->next)
	if (!oid_oidcmp (oid, ao->oid))
	    break;
    if (!ao)
    {
	ao = (zebAccessObject) nmem_malloc (zei->nmem, sizeof(*ao));
	ao->handle = NULL;
	ao->sysno = 0;
	ao->oid = odr_oiddup_nmem (zei->nmem, oid);
	ao->next = *op;
	*op = ao;
    }
    return ao;
}

void zebraExplain_addAttributeSet (ZebraExplainInfo zei, int set)
{
    oident oe;
    int oid[OID_SIZE];

    oe.proto = PROTO_Z3950;
    oe.oclass = CLASS_ATTSET;
    oe.value = (enum oid_value) set;

    if (oid_ent_to_oid (&oe, oid))
    {
	zebraExplain_announceOid (zei, &zei->accessInfo->attributeSetIds, oid);
	zebraExplain_announceOid (zei, &zei->curDatabaseInfo->
				  accessInfo->attributeSetIds, oid);
    }
}

int zebraExplain_add_attr_su(ZebraExplainInfo zei, int index_type,
			     int set, int use)
{
    struct zebSUInfoB *zsui;

    assert (zei->curDatabaseInfo);
    zebraExplain_addAttributeSet (zei, set);
    zsui = (struct zebSUInfoB *) nmem_malloc (zei->nmem, sizeof(*zsui));
    zsui->next = zei->curDatabaseInfo->attributeDetails->SUInfo;
    zei->curDatabaseInfo->attributeDetails->SUInfo = zsui;
    zei->curDatabaseInfo->attributeDetails->dirty = 1;
    zei->dirty = 1;
    zsui->info.index_type = index_type;
    zsui->info.which = ZEB_SU_SET_USE;
    zsui->info.u.su.set = set;
    zsui->info.u.su.use = use;
    zsui->info.ordinal = (zei->ordinalSU)++;
    return zsui->info.ordinal;
}

int zebraExplain_add_attr_str(ZebraExplainInfo zei, int index_type,
			      const char *index_name)
{
    struct zebSUInfoB *zsui;

    assert (zei->curDatabaseInfo);
    zsui = (struct zebSUInfoB *) nmem_malloc (zei->nmem, sizeof(*zsui));
    zsui->next = zei->curDatabaseInfo->attributeDetails->SUInfo;
    zei->curDatabaseInfo->attributeDetails->SUInfo = zsui;
    zei->curDatabaseInfo->attributeDetails->dirty = 1;
    zei->dirty = 1;
    zsui->info.index_type = index_type;
    zsui->info.which = ZEB_SU_STR;
    zsui->info.u.str = nmem_strdup(zei->nmem, index_name);
    zsui->info.ordinal = (zei->ordinalSU)++;
    return zsui->info.ordinal;
}

void zebraExplain_addSchema (ZebraExplainInfo zei, Odr_oid *oid)
{
    zebraExplain_announceOid (zei, &zei->accessInfo->schemas, oid);
    zebraExplain_announceOid (zei, &zei->curDatabaseInfo->
			      accessInfo->schemas, oid);
}

void zebraExplain_recordBytesIncrement (ZebraExplainInfo zei, int adjust_num)
{
    assert (zei->curDatabaseInfo);

    if (adjust_num)
    {
	zei->curDatabaseInfo->recordBytes += adjust_num;
	zei->curDatabaseInfo->dirty = 1;
    }
}

void zebraExplain_recordCountIncrement (ZebraExplainInfo zei, int adjust_num)
{
    assert (zei->curDatabaseInfo);

    if (adjust_num)
    {
	zei->curDatabaseInfo->recordCount += adjust_num;
	zei->curDatabaseInfo->dirty = 1;
    }
}

zint zebraExplain_runNumberIncrement (ZebraExplainInfo zei, int adjust_num)
{
    if (adjust_num)
    {
	zei->dirty = 1;
    }
    return zei->runNumber += adjust_num;
}

RecordAttr *rec_init_attr (ZebraExplainInfo zei, Record rec)
{
    RecordAttr *recordAttr;

    if (rec->info[recInfo_attr])
	return (RecordAttr *) rec->info[recInfo_attr];
    recordAttr = (RecordAttr *) xmalloc (sizeof(*recordAttr));
    rec->info[recInfo_attr] = (char *) recordAttr;
    rec->size[recInfo_attr] = sizeof(*recordAttr);
    
    recordAttr->recordSize = 0;
    recordAttr->recordOffset = 0;
    recordAttr->runNumber = zei->runNumber;
    recordAttr->staticrank = 0;
    return recordAttr;
}

static void att_loadset(void *p, const char *n, const char *name)
{
    data1_handle dh = (data1_handle) p;
    if (!data1_get_attset (dh, name))
	yaz_log(YLOG_WARN, "Directive attset failed for %s", name);
}

void zebraExplain_loadAttsets (data1_handle dh, Res res)
{
    res_trav(res, "attset", dh, att_loadset);
}

/*
     zebraExplain_addSU adds to AttributeDetails for a database and
     adds attributeSet (in AccessInfo area) to DatabaseInfo if it doesn't
     exist for the database.

     If the database doesn't exist globally (in TargetInfo) an 
     AttributeSetInfo must be added (globally).
 */
