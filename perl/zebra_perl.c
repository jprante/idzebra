#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include <yaz/yaz-util.h>
#include <yaz/proto.h>
#include <yaz/ylog.h>
#include <yaz/cql.h>
#include <yaz/pquery.h>

#include "zebra_perl.h"
#include "../recctrl/perlread.h"
#include <idzebra/data1.h>

NMEM handles;

void init (void) {
  nmem_init ();
  yaz_log_init_prefix ("ZebraPerl");
  yaz_log (YLOG_LOG, "Zebra API initialized");
}

void DESTROY (void) {
  nmem_exit ();
  yaz_log (YLOG_LOG, "Zebra API destroyed");
}   

/* Logging facilities from yaz */
void logMsg (int level, const char *message) {
  yaz_log(level, "%s", message);
}

/* debug tool for data1... maybe should be moved to data1. 
   perl can't really pass filehandles */
void data1_print_tree(data1_handle dh, data1_node *n) {
  data1_pr_tree(dh, n, stdout);
}

/* ---------------------------------------------------------------------------
  Record retrieval 
  2 phase retrieval - I didn't manage to return array of blessed references
  to wrapped structures... it's feasible, but I'll need some time 
  / pop - 2002-11-17
*/

void record_retrieve(RetrievalObj *ro,
		     ODR stream,
		     RetrievalRecord *res,
		     int pos) 
{
  int i = pos - 1;


  RetrievalRecordBuf *buf = 
    (RetrievalRecordBuf *) odr_malloc(stream, sizeof(*buf));  

  res->errCode    = ro->records[i].errCode;
  if (ro->records[i].errString) {
    res->errString  = odr_strdup(stream, ro->records[i].errString);
  } else {
    res->errString = "";
  }
  res->position   = ro->records[i].position;
  res->base       = odr_strdup(stream, ro->records[i].base);
  res->format     = (char *) 
    yaz_z3950_oid_value_to_str(ro->records[i].format, CLASS_RECSYN); 
  res->buf        = buf;
  res->buf->len   = ro->records[i].len;
  res->buf->buf   = ro->records[i].buf;
  res->score      = ro->records[i].score;
  res->sysno      = ro->records[i].sysno;

}



/* most of the code here was copied from yaz-client */
void records_retrieve(ZebraHandle zh,
		      ODR stream,
		      const char *setname,
		      const char *a_eset, 
		      const char *a_schema,
		      const char *a_format,
		      int from,
		      int to,
		      RetrievalObj *res) 
{
  static enum oid_value recordsyntax = VAL_SUTRS;
  static enum oid_value schema = VAL_NONE;
  static Z_ElementSetNames *elementSetNames = 0; 
  static Z_RecordComposition compo;
  static Z_ElementSetNames esn;
  static char what[100];
  int i;
  int oid[OID_SIZE];

  compo.which = -1;

  if (from < 1) from = 1;
  if (from > to) to = from;
  res->noOfRecords = to - from + 1;

  res->records = odr_malloc (stream, 
			     sizeof(*res->records) * (res->noOfRecords));  

  for (i = 0; i<res->noOfRecords; i++) res->records[i].position = from+i;

  if (!a_eset || !*a_eset) {
    elementSetNames = 0;
  } else {
    strcpy(what, a_eset);
    esn.which = Z_ElementSetNames_generic;
    esn.u.generic = what;
    elementSetNames = &esn;
  }

  if (!a_schema || !*a_schema) {
    schema = VAL_NONE;
  } else {
    schema = oid_getvalbyname (a_schema);
    if (schema == VAL_NONE) {
      yaz_log(YLOG_WARN,"unknown schema '%s'",a_schema);
    }
  }

  
  if (!a_format || !*a_format) {
    recordsyntax = VAL_SUTRS;
  } else {
    recordsyntax = oid_getvalbyname (a_format);
    if (recordsyntax == VAL_NONE) {
      yaz_log(YLOG_WARN,"unknown record syntax '%s', using SUTRS",a_schema);
      recordsyntax = VAL_SUTRS;
    }
  }

  if (schema != VAL_NONE) {
    oident prefschema;

    prefschema.proto = PROTO_Z3950;
    prefschema.oclass = CLASS_SCHEMA;
    prefschema.value = schema;
    
    compo.which = Z_RecordComp_complex;
    compo.u.complex = (Z_CompSpec *)
      odr_malloc(stream, sizeof(*compo.u.complex));
    compo.u.complex->selectAlternativeSyntax = (bool_t *) 
      odr_malloc(stream, sizeof(bool_t));
    *compo.u.complex->selectAlternativeSyntax = 0;
    
    compo.u.complex->generic = (Z_Specification *)
      odr_malloc(stream, sizeof(*compo.u.complex->generic));
    compo.u.complex->generic->which = Z_Schema_oid;
    compo.u.complex->generic->schema.oid = (Odr_oid *)
      odr_oiddup(stream, oid_ent_to_oid(&prefschema, oid));
    if (!compo.u.complex->generic->schema.oid)
      {
	/* OID wasn't a schema! Try record syntax instead. */
	prefschema.oclass = CLASS_RECSYN;
	compo.u.complex->generic->schema.oid = (Odr_oid *)
	  odr_oiddup(stream, oid_ent_to_oid(&prefschema, oid));
      }
    if (!elementSetNames)
      compo.u.complex->generic->elementSpec = 0;
    else
      {
	compo.u.complex->generic->elementSpec = (Z_ElementSpec *)
	  odr_malloc(stream, sizeof(Z_ElementSpec));
	compo.u.complex->generic->elementSpec->which =
	  Z_ElementSpec_elementSetName;
	compo.u.complex->generic->elementSpec->u.elementSetName =
	  elementSetNames->u.generic;
      }
    compo.u.complex->num_dbSpecific = 0;
    compo.u.complex->dbSpecific = 0;
    compo.u.complex->num_recordSyntax = 0;
    compo.u.complex->recordSyntax = 0;
  } 
  else if (elementSetNames) {
    compo.which = Z_RecordComp_simple;
    compo.u.simple = elementSetNames;
  }

  if (compo.which == -1) {
    api_records_retrieve (zh, stream, setname, 
			    NULL, 
			    recordsyntax,
			    res->noOfRecords, res->records);
  } else {
    api_records_retrieve (zh, stream, setname, 
			    &compo,
			    recordsyntax,
			    res->noOfRecords, res->records);
  }

}
 
int zebra_cql2pqf (cql_transform_t ct, 
		   const char *query, char *res, int len) {
  
  int status;
  const char *addinfo = "";
  CQL_parser cp = cql_parser_create();

  if (status = cql_parser_string(cp, query)) {
    cql_parser_destroy(cp);
    return (status);
  }

  if (cql_transform_buf(ct, cql_parser_result(cp), res, len)) {
    status = cql_transform_error(ct, &addinfo);
    yaz_log (YLOG_WARN,"Transform error %d %s\n", status, addinfo ? addinfo : "");
    cql_parser_destroy(cp);
    return (status);
  }

  cql_parser_destroy(cp);
  return (0);
}

void zebra_scan_PQF (ZebraHandle zh,
		     ScanObj *so,
		     ODR stream,
		     const char *pqf_query)
{
  Z_AttributesPlusTerm *zapt;
  Odr_oid *attrsetid;
  const char* oidname;
  oid_value attributeset;
  ZebraScanEntry *entries;
  int i, class;

  yaz_log(YLOG_DEBUG,  
       "scan req: pos:%d, num:%d, partial:%d", 
       so->position, so->num_entries, so->is_partial);

  zapt = p_query_scan (stream, PROTO_Z3950, &attrsetid, pqf_query);

  oidname = yaz_z3950oid_to_str (attrsetid, &class); 
  yaz_log (YLOG_DEBUG, "Attributreset: %s", oidname);
  attributeset = oid_getvalbyname(oidname);

  if (!zapt) {
    yaz_log (YLOG_WARN, "bad query %s\n", pqf_query);
    odr_reset (stream);
    return;
  }

  so->entries = (scanEntry *)
    odr_malloc (stream, sizeof(so->entries) * (so->num_entries));


  zebra_scan (zh, stream, zapt, attributeset, 
	      &so->position, &so->num_entries, 
	      (ZebraScanEntry **) &so->entries, &so->is_partial);

  yaz_log(YLOG_DEBUG, 
       "scan res: pos:%d, num:%d, partial:%d", 
       so->position, so->num_entries, so->is_partial);
}

scanEntry *getScanEntry(ScanObj *so, int pos) {
  return (&so->entries[pos-1]);
}

#if 1
void Filter_store_buff (struct perl_context *context, char *buff, size_t len)
{
    dSP;
    
    ENTER;
    SAVETMPS;
    
    PUSHMARK(SP) ;
    XPUSHs(context->filterRef);
    XPUSHs(sv_2mortal(newSVpv(buff, len)));  
    PUTBACK ;
    call_method("_store_buff", 0);
    SPAGAIN ;
    PUTBACK ;
    
    FREETMPS;
    LEAVE;
}

/*  The "file" manipulation function wrappers */
int grs_perl_readf(struct perl_context *context, size_t len)
{
    int r;
    char *buf = (char *) xmalloc (len+1);
    r = (*context->readf)(context->fh, buf, len);
    if (r > 0) Filter_store_buff (context, buf, r);
    xfree (buf);
    return (r);
}

int grs_perl_readline(struct perl_context *context)
{
    int r;
    char *buf = (char *) xmalloc (4096);
    char *p = buf;
    
    while ((r = (*context->readf)(context->fh,p,1)) && (p-buf < 4095)) {
	p++;
	if (*(p-1) == 10) break;
    }
    
    *p = 0;
    
    if (p != buf) Filter_store_buff (context, buf, p - buf);
    xfree (buf);
    return (p - buf);
}

char grs_perl_getc(struct perl_context *context)
{
    int r;
    char *p;
    if ((r = (*context->readf)(context->fh,p,1))) {
	return (*p);
    } else {
	return (0);
    }
}

off_t grs_perl_seekf(struct perl_context *context, off_t offset)
{
    return ((*context->seekf)(context->fh, offset));
}

off_t grs_perl_tellf(struct perl_context *context)
{
    return ((*context->tellf)(context->fh));
}

void grs_perl_endf(struct perl_context *context, off_t offset)
{
    (*context->endf)(context->fh, offset);
}

/* Get pointers from the context. Easyer to wrap this by SWIG */
data1_handle *grs_perl_get_dh(struct perl_context *context)
{
    return(&context->dh);
}

NMEM *grs_perl_get_mem(struct perl_context *context)
{
    return(&context->mem);
}

/* Set the result in the context */
void grs_perl_set_res(struct perl_context *context, data1_node *n)
{
    context->res = n;
}
#endif
