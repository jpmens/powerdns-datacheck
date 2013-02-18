#include <stdio.h>
#include <string.h>
#include <my_global.h>
#include <my_sys.h>
#include <mysql.h>
#include <m_string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <cdb.h>
#include <pcre.h>
#include <arpa/inet.h>

/*
 * Watch out for SeLinux when defining this path!
 */

#define DB	"/home/vagrant/udf/data.cdb"
#define OVECCOUNT 30    /* should be a multiple of 3 */
#define BLEN		1024

#define FIELDSEP	" \t"
#define T_NAME		(1)
#define T_DATA		(2)
#define T_NUMBER	(1)
#define T_STRING	(2)

struct _info {
	char *buf;
	char *msg;
	char *reason;		// explanation for a fail
	int fd;			// fd of CDB database
	struct cdb cdb;
	struct cdb_find cdbf;
};

static int do_regex_checks(struct _info *info, char *rr_type, char *key, char *rr_data);
static long cdb_getval(struct _info *info, char *keyname);

static my_bool checkpart_init(UDF_INIT *iid, UDF_ARGS *args, char *message)
{
	struct _info *info;

	if (args->arg_count != 2) {
		strmov(message,"Need two STRING args");
		return (1);
	}

	if ((args->arg_type[0] != STRING_RESULT) ||
		(args->arg_type[1] != STRING_RESULT)) {
		strmov(message,"args must be STRING");
		return (1);
	}

	args->arg_type[0] = STRING_RESULT;
	args->arg_type[1] = STRING_RESULT;


	info = (struct _info *)malloc(sizeof(struct _info));
	info->msg = strdup("Hello JP");
	info->buf = calloc(sizeof(char), BLEN);
	info->reason = calloc(sizeof(char), BLEN);

        if (((info->fd = open(DB, O_RDONLY)) == -1) ||
			(cdb_init(&info->cdb, info->fd) != 0)) {
		fprintf(stderr, "Can't open CDB database at %s: %s\n", DB, strerror(errno));
		return (1);
	}

	iid->ptr = (char *)info;
	iid->max_length = BLEN;
	iid->maybe_null = 0;
	iid->const_item = 0;

	return 0;
}

static void checkpart_deinit(UDF_INIT *iid)
{
	struct _info *info = (struct _info *)iid->ptr;

	cdb_free(&info->cdb);
	close(info->fd);

	fprintf(stderr, "%s\n", info->msg);
	free(info->msg);
	free(info->reason);
	free(iid->ptr);
}

static char *checkpart(UDF_INIT *iid, UDF_ARGS *args, char *result,
             unsigned long *length, char *isnull, char *error, int check_type)
{
	uint len;
	struct _info *info = (struct _info *)iid->ptr;
	char rr_type[BLEN], rr_rdata[BLEN];
	char key[BLEN];
	int n;

	*info->reason = '\0';		// Clear out reason

#if 0
	if (!args->args[0] || !(len = args->lengths[0])) {
		*isnull = 1;
		return 0;
	}
#endif

	len = (args->lengths[0] > (sizeof(rr_type) - 1)) ? sizeof(rr_type) - 1 : args->lengths[0];
	memcpy(rr_type, args->args[0], len);
	rr_type[len] = 0;

	len = (args->lengths[1] > (sizeof(rr_rdata) - 1)) ? sizeof(rr_rdata) - 1 : args->lengths[1];
	memcpy(rr_rdata, args->args[1], len);
	rr_rdata[len] = 0;

	/*
	 * Search CDB for all keys pertaining to this RR type
	 */

	switch (check_type) {
		case T_DATA:
			sprintf(key, "%s:content", rr_type);
			break;
		case T_NAME:
			sprintf(key, "%s:name", rr_type);
			break;
		default:
			strcpy(error, "Impossible T_type");
			return (0);
	}

	// fprintf(stderr, "Going to use key [%s] on rdata [%s]\n", key, rr_rdata);
	n = do_regex_checks(info, rr_type, key, rr_rdata);

	if (n > 0) {
		strcpy(result, "Y");
		*error = 0;
	} else {
		fprintf(stderr, "%s\n", info->reason);
		sprintf(result, "N:%s", info->reason);
		*error = 0;	// force OK: don't see another way of returning error msg
	}
		

	// sprintf(result, "Nay %d checks for key %s succeeded", n, key);
	*length = strlen(result);

	return (result);
}


/*
 * checkrr_*()
 *
 * Check the rdata of a resource record, given its type.
 *
 * ARG1: rrtype ['A', 'AAAA', 'SOA', etc. ]
 * ARG2: rdata as string
 */

my_bool checkrr_init(UDF_INIT *iid, UDF_ARGS *args, char *message)
{
	return checkpart_init(iid, args, message);
}

void checkrr_deinit(UDF_INIT *iid)
{
	checkpart_deinit(iid);
}

char *checkrr(UDF_INIT *iid, UDF_ARGS *args, char *result,
             unsigned long *length, char *isnull, char *error)
{
	return checkpart(iid, args, result, length, isnull, error, T_DATA);
}

/*
 * checkname_*()
 *
 * Check validity of domain names, given their types
 *
 * ARG1: rrtype
 * ARG2: owner name
 */

my_bool checkname_init(UDF_INIT *iid, UDF_ARGS *args, char *message)
{
	return checkpart_init(iid, args, message);
}

void checkname_deinit(UDF_INIT *iid)
{
	checkpart_deinit(iid);
}

char *checkname(UDF_INIT *iid, UDF_ARGS *args, char *result,
             unsigned long *length, char *isnull, char *error)
{
	return checkpart(iid, args, result, length, isnull, error, T_NAME);
}


static int match(char *pattern, char *subject)
{
	pcre *re;
	const char *error;
	int erroffset;
	int ovector[OVECCOUNT];
	int subject_length = strlen(subject);
	int rc;

	/* Compile the RE and handle errors */

	re = pcre_compile(
		pattern,              /* the pattern */
		0,                    /* default options */
		&error,               /* for error message */
		&erroffset,           /* for error offset */
		NULL);                /* use default character tables */
	if (re == NULL) {
		fprintf(stderr, "PCRE compilation failed at offset %d: %s\n", erroffset, error);
		return 0;
	}

	/* Match pattern against subject string (one match) */


	rc = pcre_exec(
		re,                   /* the compiled pattern */
		NULL,                 /* no extra data - we didn't study the pattern */
		subject,              /* the subject string */
		subject_length,       /* the length of the subject */
		0,                    /* start at offset 0 in the subject */
		0,                    /* default options */
		ovector,              /* output vector for substring information */
		OVECCOUNT);           /* number of elements in the output vector */

	if (rc < 0) {
		switch(rc) {
			case PCRE_ERROR_NOMATCH:
				// printf("No match\n");
				break;
			default:
				fprintf(stderr, "Matching error %d\n", rc);
				break;
		}
		pcre_free(re);
		return (0);
	}

	/* Match succeded */

	pcre_free(re);
	return (1);
}

/*
 * Return 1 if content of string is a valid IPv4 or IPv6
 * address representation.
 */

static int func_ip(struct _info *info, char *rr_type, char *string)
{
	struct sockaddr_in sa;
	struct sockaddr_in6 sa6;

	if (!strcmp(rr_type, "A")) {
		if (inet_pton(AF_INET, string, &(sa.sin_addr)) == 1)
			return (1);
	} else if (!strcmp(rr_type, "AAAA")) {
		if (inet_pton(AF_INET6, string, &(sa6.sin6_addr)) == 1)
			return (1);
	}
	sprintf(info->reason, "[%s] is not a valid IP address for %s",
		string, rr_type);
	return (0);
}

/*
 * Return 1 if content of string is a valid SOA record
 */

static int func_soa(struct _info *info, char *rr_type, char *soa_rdata)
{
	char *token, *string, *tofree, *rest;
	// char *mname, *rname, *serial, *refresh, *retry, *expiry, *minimum;
	int n, m, nmatches = 0;
	struct _sp {
		char *name;		// part name (e.g. "mname")
		char *val;		// value
		int type;		// T_NUMBER, T_STRING
	};

	/* don't change the order! */
	static struct _sp plist[] = {
		{ "mname",	NULL,	T_STRING },
		{ "rname",	NULL,	T_STRING },
		{ "serial",	NULL,	T_NUMBER },
		{ "refresh",	NULL,	T_NUMBER },
		{ "retry",	NULL,	T_NUMBER },
		{ "expiry",	NULL,	T_NUMBER },
		{ "minimum",	NULL,	T_NUMBER },
		{ NULL,		NULL,	0 }
	};


	tofree = string = strdup(soa_rdata);

	/*
	 * Split up SOA rdata into individual fields
	 */

	for (n = 0; plist[n].name; n++) {
		if ((plist[n].val = strtok_r(string, FIELDSEP, &token)) == NULL) {
			sprintf(info->reason, "SOA contains too few fields");
			return (1);
		}
		string = NULL;
	}
	if ((rest = strtok_r(NULL, FIELDSEP, &token)) != NULL) {
		sprintf(info->reason, "SOA contains too many fields");
		return (0);
	}

	for (n = 0; plist[n].name; n++) {
		char keyname[BLEN];

		// fprintf(stderr, "SOA %s == %s\n", plist[n].name, plist[n].val);

		sprintf(keyname, "SOA:%s", plist[n].name);

		m = do_regex_checks(info, rr_type, keyname, plist[n].val);
		// fprintf(stderr, "LOOKUP(%s) for [%s] returns %d\n", keyname, plist[n].val, m);

		if (m) {
			++nmatches;
		}

		/* For numeric components of the SOA rdata (refresh, expire, etc.)
		 * check for an :equals key (in which case the value must match). 
		 * If the :equals key doesn't exist, attempt :min and :max (both 
		 * optional).
		 */

		if (plist[n].type == T_NUMBER) {
			long nval;

			/*
			 * Equality:
			 */

			sprintf(keyname, "SOA:%s:equals", plist[n].name);

			nval = cdb_getval(info, keyname);
			if ((nval != -1L) && (nval != atol(plist[n].val))) {
				sprintf(info->reason, "SOA field %s isn't equal %s",
						plist[n].name, plist[n].val);
				nmatches--;
				return (0);

			}

			/*
			 * Min:
			 */

			sprintf(keyname, "SOA:%s:min", plist[n].name);

			nval = cdb_getval(info, keyname);
			if ((nval != -1L) && (atol(plist[n].val) <= nval)) {
				sprintf(info->reason, "SOA `%s' doesn't satisfy min %ld: %s",
						plist[n].name, nval, plist[n].val);
				nmatches--;
				return (0);

			}

			/*
			 * Max:
			 */
			

			sprintf(keyname, "SOA:%s:max", plist[n].name);

			nval = cdb_getval(info, keyname);
			if ((nval != -1L) && (nval < atol(plist[n].val))) {
				sprintf(info->reason, "SOA `%s' doesn't satisfy max %ld: %s",
						plist[n].name, nval, plist[n].val);
				nmatches--;
				return (0);

			}
		}
	}

	free(tofree);

	/* Have we successfully matched all tokens in SOA rdata? */

	if (nmatches == 7) {	
		return (1);
	}
	
	strcpy(info->reason, "SOA checking fails a test");
	return (0);
}

static long cdb_getval(struct _info *info, char *keyname)
{
	unsigned keylen = strlen(keyname), vlen, vpos;
	long nval;
	char *val;

	if (cdb_find(&info->cdb, keyname, keylen) > 0) {
		vpos = cdb_datapos(&info->cdb);
		vlen = cdb_datalen(&info->cdb);
		val = malloc(vlen);
		cdb_read(&info->cdb, val, vlen, vpos);
		nval = atol(val);
		free(val);
		return (nval);
	}


	return (-1L);

}

static int do_regex_checks(struct _info *info, char *rr_type, char *key, char *rr_rdata)
{
        char *pattern;
	unsigned keylen = strlen(key), patlen, vpos;
	int nchecks = 0;

	if (cdb_findinit(&info->cdbf, &info->cdb, key, keylen) <= 0) {
		return (-1);
	}

	while (cdb_findnext(&info->cdbf) > 0) {
		int m;

		vpos = cdb_datapos(&info->cdb);
		patlen = cdb_datalen(&info->cdb);
		pattern = malloc(patlen + 1);
		cdb_read(&info->cdb, pattern, patlen, vpos);
		pattern[patlen] = '\0';

		if (*pattern == '@') {
			/*
			 * Handle special function for data check
			 */

			if (!strcmp(pattern, "@IP")) {
				m = func_ip(info, rr_type, rr_rdata);
				if (m) {
					++nchecks;
				} else {
					return (0);
				}
			} else if (!strcmp(pattern, "@SOA")) {
				m = func_soa(info, rr_type, rr_rdata);
				if (m) {
					++nchecks;
				} else {
					return (0);
				}
			}
		} else {

			m = match(pattern, rr_rdata);
//			fprintf(stderr, "*** match %s  ", m ? "TRUE " : "false");
//				fprintf(stderr, "%s\n", pattern);

			if (m) {
				++nchecks;
				// FIXME: can return now
			} else {
				sprintf(info->reason, "%s [%s] fails regexp [%s]",
					rr_type, rr_rdata, pattern);
			}
		}

		free(pattern);
	}

	return (nchecks);
}

#define E_NOSPEC        "Unspecified error raised (JP)\0"

my_bool
raise_error_init (UDF_INIT * iid, UDF_ARGS * args, char *message)
{
        unsigned int n;

        if (args->arg_count == 1 && args->arg_type[0] == STRING_RESULT) {
                n = (args->lengths[0] > (MYSQL_ERRMSG_SIZE - 1)) ?
                        MYSQL_ERRMSG_SIZE - 1 : args->lengths[0];
                memcpy(message, args->args[0], n);
                message[n] = 0;
        } else {
                memcpy(message, E_NOSPEC, strlen(E_NOSPEC) + 1);
        }

        return 1;       // Force MySQL to fail
}

longlong
raise_error (UDF_INIT * iid, UDF_ARGS * args, char *isnull, char *error)
{
        return 0L;
}

void
raise_error_deinit (UDF_INIT * iid)
{
}
