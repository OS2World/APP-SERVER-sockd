/*
 * File: cmds.h
 *
 * SOCKS daemon for OS/2.
 *
 * Command codes and table.
 *
 * Bob Eager   October 1997
 *
 */

/* Internal command codes */

#define	CMD_PORT		1
#define	CMD_TRUSTED_INTERFACE	2
#define	CMD_BIND_INTERFACE	3
#define	CMD_BAD			4

static	struct {
	UCHAR	*cmdname;		/* Command name */
	INT	cmdcode;		/* Command code */
} cmdtab[] = {
	{ "PORT",		CMD_PORT },
	{ "TRUSTED_INTERFACE",	CMD_TRUSTED_INTERFACE },
	{ "BIND_INTERFACE",	CMD_BIND_INTERFACE },
	{ "",			CMD_BAD }	/* End of table marker */
};

/*
 * End of file: cmds.h
 *
 */

