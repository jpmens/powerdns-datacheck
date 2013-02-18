#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <cdb.h>
#include <pcre.h>

#define DB	"data.cdb"

#define OVECCOUNT 30    /* should be a multiple of 3 */

int match(char *pattern, char *subject)
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
		printf("PCRE compilation failed at offset %d: %s\n", erroffset, error);
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
				printf("Matching error %d\n", rc);
				break;
		}
		pcre_free(re);
		return (0);
	}

	/* Match succeded */

	pcre_free(re);
	return (1);
}

int checks(char *key, char *buf)
{
        struct cdb cdb;
	struct cdb_find cdbf;
	int fd;
        char *data;
        unsigned keylen = strlen(key), datalen, vpos;

        if (((fd = open(DB, O_RDONLY)) == -1) ||
			(cdb_init(&cdb, fd) != 0)) {
		printf("Can't open database: %s\n", strerror(errno));
		return (-1);
	}


	if (cdb_findinit(&cdbf, &cdb, key, keylen) <= 0) {
		return (-1);
	}

	while (cdb_findnext(&cdbf) > 0) {
		char *pattern, *subject;
		int m;

		vpos = cdb_datapos(&cdb);
		datalen = cdb_datalen(&cdb);
		data = malloc(datalen + 1);
		cdb_read(&cdb, data, datalen, vpos);
		data[datalen] = '\0';

		pattern = data;
		subject = buf;
		m = match(pattern, subject);
		printf("*** match %s  ", m ? "TRUE " : "false");
			printf("%s\n", data);

		free(data);
	}

	cdb_free(&cdb);
	close(fd);
	return (0);
}

int main(int argc, char **argv, char **envp)
{
	FILE *fp;
	char buf[BUFSIZ];
	char *key;

	if (argc != 2) {
		puts("usage: prog keyname");
		return 1;
	}

	key = argv[1];

	if ((fp = fopen("input", "r")) == NULL) {
		perror("input");
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[strlen(buf)-1] == '\n')
			buf[strlen(buf)-1] = 0;

		printf("---> [%s]     (%s)\n", buf, key);
//		checks("name", buf);		// check name* first
		checks(key, buf);		// check type:name then
		printf("\n\n\n");
	}
	fclose(fp);
	return 0;
}
