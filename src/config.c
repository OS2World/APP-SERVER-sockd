/*
 * File: config.c
 *
 * SOCKS daemon for OS/2.
 *
 * Configuration file handler.
 *
 * Bob Eager   October 1997
 *
 */

#pragma	strings(readonly)

#include "sockd.h"
#include "cmds.h"
#include "log.h"

#pragma	alloc_text(init_seg, read_config)
#pragma	alloc_text(init_seg, config_error)
#pragma	alloc_text(init_seg, get_interface)
#pragma	alloc_text(init_seg, getcmd)

#define	MAXLINE		200		/* Maximum length of a config line */

#define	SOCKSSERVICE	"socks"		/* Name of SOCKS service */
#define	TCP		"tcp"		/* TCP protocol */

/* Forward references */

static	VOID	config_error(INT, PUCHAR, ...);
static	VOID	get_interface(PCONFIG, INT, PUCHAR, PINT);
static	INT	getcmd(PUCHAR);

/* Local storage */

static	PUCHAR	interfaces[] = {	/* Valid interface names */
"sl0", "sl1", "sl2", "sl3",
"lan0", "lan1", "lan2", "lan3", "lan4", "lan5", "lan6", "lan7"
};


/*
 * Read and parse the configuration file specified by 'configfile'.
 *
 * Returns:
 *	Number of errors encountered.
 *	Any error messages have already been issued.
 *
 * The configuration information is returned in the structure 'config'.
 *
 */

INT read_config(PUCHAR direnv, PUCHAR configfile, PCONFIG config)
{	INT i;
	INT line = 0;
	INT errors = 0;
	ULONG addr;
	PUCHAR p, q, r, temp;
	PINADDRENT pinta;
	FILE *fp;
	PSERV socksserv;
	UCHAR buf[MAXLINE];
	UCHAR filename[CCHMAXPATH];
	BOOL port_seen = FALSE;

	p = getenv(direnv);
	if(p == (PUCHAR) NULL) {
		config_error(0, "environment variable %s is not set", direnv);
		return(++errors);
	}
	strcpy(filename, p);
	p = p + strlen(filename) - 1;	/* Point to last character */
	if(*p != '/' && *p != '\\') strcat(filename, "\\");
	strcat(filename, configfile);

	socksserv = getservbyname(SOCKSSERVICE, TCP);
	if(socksserv == (PSERV) NULL) {
		config_error(0, "cannot find port for %s/%s service",
			SOCKSSERVICE, TCP);
		return(++errors);
	}

	/* Set defaults */

	memset(config, 0, sizeof(CONFIG));
	config->port = socksserv->s_port;
	config->ntinterfaces = 0;
	config->tinterface_list = (PINADDRENT) NULL;
	config->bind_status = BIND_DISABLED;

	fp = fopen(filename, "r");
	if(fp == (FILE *) NULL) {
		config_error(0, "cannot open configuration file %s", filename);
		return(++errors);
	}

	for(;;) {
		p = fgets(buf, MAXLINE, fp);
		if(p == (PUCHAR) NULL) break;
		temp = p + strlen(p) - 1;	/* Point to last character */
		if(*temp == '\n') *temp = '\0';	/* Remove any newline */
		line++;

		p = strtok(buf, " \t");
		q = strtok(NULL, " \t");
		r = strtok(NULL, " \t");

		/* Skip non-information lines */

		if((p == (PUCHAR) NULL) ||	/* No tokens */
		   (*p == '#') ||		/* Comment line */
		   (*p == '\n'))		/* Empty line */
			continue;

		switch(getcmd(p)) {
			case CMD_PORT:
				if(r != (PUCHAR) NULL) {
					config_error(
						line,
						"syntax error (extra on end)");
					errors++;
					continue;
				}
				if(q == (PUCHAR) NULL) {
					config_error(
						line,
						"no port number after PORT "
						"command");
					errors++;
					break;
				}
				if(port_seen == TRUE) {
					config_error(
						line,
						"only one PORT command "
						"permitted");
					errors++;
					break;
				}
				port_seen = TRUE;
				p = q;
				while(*p != '\0') {
					if(!isdigit(*p)) {
						config_error(
							line,
							"invalid port number "
							"'%s'",
							q);
						errors++;
						break;
					}
					p++;
				}
				if(*p == '\0')
					config->port = htons((USHORT) atoi(q));
				break;

			case CMD_TRUSTED_INTERFACE:
				if(r != (PUCHAR) NULL) {
					config_error(
						line,
						"syntax error (extra on end)");
					errors++;
					continue;
				}
				if(q == (PUCHAR) NULL) {
					config_error(
						line,
						"no interface address after "
						"TRUSTED_INTERFACE command");
					errors++;
					break;
				}
				addr = inet_addr(q);
				if(addr == INADDR_NONE) {
					config_error(
						line,
						"malformed interface address "
						"'%s'",
						q);
					errors++;
					break;
				}
				pinta = (PINADDRENT) malloc(sizeof(INADDRENT));
				if(pinta == (PUCHAR) NULL) {
					config_error(
						0,
						"cannot allocate memory");
					errors++;
					break;
				}
				pinta->interface_address.s_addr = addr;
				pinta->next = (PINADDRENT) NULL;
				if(config->tinterface_list == (PINADDRENT) NULL) {
					config->tinterface_list = pinta;
				} else {
					PINADDRENT temp = config->tinterface_list;

					while(temp->next != (PINADDRENT) NULL)
						temp = temp->next;
					temp->next = pinta;
				}
				config->ntinterfaces++;
				break;

			case CMD_BIND_INTERFACE:
				if(r != (PUCHAR) NULL) {
					config_error(
						line,
						"syntax error (extra on end)");
					errors++;
					continue;
				}
				if(q == (char *) NULL) {
					config_error(
						line,
						"no interface (address) after "
						"BIND_INTERFACE command");
					errors++;
					break;
				}
				if(config->bind_status != BIND_DISABLED) {
					config_error(
						line,
						"only one BIND_INTERFACE "
						"command permitted");
					errors++;
					break;
				}
				get_interface(config, line, q, &errors);
				break;

			default:
				config_error(
					line,
					"unrecognised command '%s'", p);
				errors++;
				break;
		}
	}

	fclose (fp);

	if(config->ntinterfaces == 0) {
		config_error(0, "at least one trusted interface must be"
				" specified");
		return(++errors);
	}

#ifdef	DEBUG
	trace("config: using port %d", ntohs(config->port));
	trace(
		"config: number of trusted interfaces = %d",
		config->ntinterfaces);
	pinta = config->tinterface_list;
	i = 1;
	while(pinta != (PINADDRENT) NULL) {
		trace("config: trusted interface %d is %s",
			i, inet_ntoa(pinta->interface_address));
		i++;
		pinta = pinta->next;
	}
	trace(
		"config: bind status: %s",
		config->bind_status == BIND_DISABLED ? "disabled" :
		config->bind_status == BIND_ADDRESS  ? inet_ntoa(config->bind_address) :
						       config->bind_device);
#endif
	return(errors);
}


/*
 * Check command in 's' for validity, and return command code.
 * Case is immaterial.
 *
 * Returns CMD_BAD if command not recognised.
 *
 */

static INT getcmd(PUCHAR s)
{	INT i;

	for(i = 0; ; i++) {
		if(cmdtab[i].cmdcode == CMD_BAD) return(CMD_BAD);
		if(stricmp(s, cmdtab[i].cmdname) == 0) break;
	}

	return(cmdtab[i].cmdcode);
}


/*
 * Validate and process a bind interface specification.
 * This can take one of two forms:
 *	(a)	an IP address
 *	(b)	an interface name. The IP address is determined dynamically;
 *		the interface itself may not exist at this time, since it
 *		may be an slN interface.
 *
 */

static VOID get_interface(PCONFIG config, INT line, PUCHAR param, PINT errors)
{	ULONG addr;
	INT i;

	/* See if this is an interface name */

	for(i = 0; i < sizeof(interfaces)/sizeof(PUCHAR); i++) {
		if(stricmp(param, interfaces[i]) == 0) {
			config->bind_device[IFNAMSIZ-1] = '\0';
			strncpy(config->bind_device, param, IFNAMSIZ - 1);
			config->bind_status = BIND_INTERFACE;
			return;
		}
	}

	addr = inet_addr(param);

	if(addr == INADDR_NONE) {
		config_error(
			line,
			"malformed interface name or address '%s'",
			param);
		(*errors)++;
		return;
	}
	config->bind_address.s_addr = addr;
	config->bind_status = BIND_ADDRESS;
}


/*
 * Output configuration error message to standard error in printf style,
 * with a copy to the logfile.
 *
 */

static VOID config_error(INT line, PUCHAR mes, ...)
{	va_list ap;
	UCHAR buf[MAXLOG];
	UCHAR logmsg[MAXLOG];

	va_start(ap, mes);
	vsprintf(buf, mes, ap);
	va_end(ap);

	if(line == 0)
		sprintf(logmsg, "config: %s", buf);
	else
		sprintf(logmsg, "config: line %d: %s", line, buf);

	error(logmsg);
	dolog(logmsg);
}

/*
 * End of file: config.c
 *
 */
