/*
 * File: sockd.c
 *
 * Simple SOCKS daemon for OS/2.
 *
 * Main program
 *
 * Bob Eager   October 1997
 *
 */

/*
 * History:
 *	1.0	Initial version.
 *
 */

#pragma	strings(readonly)

#include "sockd.h"
#include "log.h"

#pragma	alloc_text(init_seg, main)
#pragma	alloc_text(init_seg, error)
#pragma	alloc_text(init_seg, fix_domain)
#pragma	alloc_text(init_seg, log_startup)
#pragma	alloc_text(init_seg, putusage)

#define	CONFIGFILE	"SOCKD.Cnf"	/* Name of configuration file */
#define	LOGFILE		"SOCKD.Log"	/* Name of log file */
#define	ETC		"ETC"		/* Environment variable for misc files */

/* Forward references */

static	VOID	fix_domain(PUCHAR);
static	VOID	log_startup(VOID);
static	VOID	putusage(VOID);

/* Local storage */

static	CONFIG	config;
static	UCHAR 	myname[MAXHOSTNAMELEN+1];
static	PUCHAR	progname;

/* Help text */

static	const	UCHAR *helpinfo[] = {
"%s: SOCKS daemon",
"Synopsis: %s [options]",
" Options:",
"    -h           display this help",
""
};


/*
 * Parse arguments and handle options.
 *
 */

INT main(INT argc, UCHAR *argv[])
{	INT i, rc;
	BOOL ok;
	PUCHAR argp;
	PUCHAR p;

	progname = strrchr(argv[0], '\\');
	if(progname != (PUCHAR) NULL)
		progname++;
	else
		progname = argv[0];
	p = strchr(progname, '.');
	if(p != (PUCHAR) NULL) *p = '\0';
	strlwr(progname);

	tzset();			/* Set time zone */
	res_init();			/* Initialise resolver */

	/* Process input options */

	for(i = 1; i < argc; i++) {
		argp = argv[i];
		if(argp[0] == '-') {		/* Option */
			switch(argp[1]) {
				case 'h':	/* Display help */
					putusage();
					exit(EXIT_SUCCESS);

				case '\0':
					error("missing flag after '-'");
					exit(EXIT_FAILURE);

				default:
					error("invalid flag '%c'", argp[1]);
					exit(EXIT_FAILURE);
			}
		} else {
			error("invalid argument '%s'", argp);
			exit(EXIT_FAILURE);
		}
	}

	rc = sock_init();		/* Initialise socket library */
	if(rc != 0) {
		error("INET.SYS not running");
		exit(EXIT_FAILURE);
	}

	/* Get the host name of this server; if not possible, set it to the
	   dotted address. */

	rc = gethostname(myname, sizeof(myname));
	if(rc != 0) {
		struct in_addr myaddr;

		myaddr.s_addr = htonl(gethostid());
		sprintf(myname, "[%s]", inet_ntoa(myaddr));
	} else {
		fix_domain(myname);
	}

	/* Start logging */

	rc = open_logfile(ETC, LOGFILE);
	if(rc != LOG_OK) {
		error(
			"logging initialisation failed - %s",
			rc == LOG_NOENV ? "environment variable "ETC" not set" :
					  "file open failed");
		exit(EXIT_FAILURE);
	}
	log_startup();

	/* Read configuration */

	rc = read_config(ETC, CONFIGFILE, &config);
	if(rc != 0) {
		error(
			"%d configuration error%s - see logfile",
			rc, rc == 1 ? "" : "s");
		exit(EXIT_FAILURE);
	}

	/* Run the server */

	ok = server(&config);

	/* Shut down */

	close_logfile();

	return(ok == TRUE ? EXIT_SUCCESS : EXIT_FAILURE);
}


/*
 * Print message on standard error in printf style,
 * accompanied by program name.
 *
 */

VOID error(PUCHAR mes, ...)
{	va_list ap;

	fprintf(stderr, "%s: ", progname);

	va_start(ap, mes);
	vfprintf(stderr, mes, ap);
	va_end(ap);

	fputc('\n', stderr);
}


/*
 * Check for a full domain name; if not present, add default domain name.
 *
 */

static VOID fix_domain(PUCHAR name)
{	if(strchr(name, '.') == NULL && _res.defdname[0] != '\0') {
		strcat(name, ".");
		strcat(name, _res.defdname);
	}
}


/*
 * Log details of the daemon startup.
 *
 */

static VOID log_startup(VOID)
{	time_t tod;
	UCHAR timeinfo[35];
	UCHAR buf[100];

	time(&tod);
	strftime(
		timeinfo,
		sizeof(timeinfo),
		"on %a %d %b %Y at %X %Z",
		localtime(&tod));
	sprintf(
		buf,
		"%s: v%d.%d started on %s, %s",
		progname,
		VERSION,
		EDIT,
		myname,
		timeinfo);
	fprintf(stdout, "%s\n", buf);

	sprintf(
		buf,
		"v%d.%d started on %s",
		VERSION,
		EDIT,
		myname);
	dolog(buf);
}


/*
 * Output program usage information.
 *
 */

static VOID putusage(VOID)
{	PUCHAR *p = (PUCHAR *) helpinfo;
	PUCHAR q;

	for(;;) {
		q = *p++;
		if(*q == '\0') break;

		fprintf(stderr, q, progname);
		fputc('\n', stderr);
	}
	fprintf(stderr, "\nThis is version %d.%d\n", VERSION, EDIT);
}

/*
 * End of file: sockd.c
 *
 */
