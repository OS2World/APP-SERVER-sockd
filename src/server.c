/*
 * File: server.c
 *
 * SOCKS daemon for OS/2.
 *
 * Protocol handler for server
 *
 * Bob Eager   October 1997
 *
 */

#pragma	strings(readonly)

#include "sockd.h"
#include "log.h"

#define	BIND_TIMEOUT	120		/* Bind timeout in seconds */
#define	IOBUFSIZE	4096		/* Size of conversation I/O buffer */
#define	MAXUSER		100		/* Maximum length of user ID */
#define	READ_TIMEOUT	300		/* Read timeout in seconds */
#define	THREAD_STACK	16384		/* Stack size for conversation threads */

/* Forward references */

static	VOID		catch_signal(INT);
static	VOID		do_handle_connection(PTHREADINFO);
static	VOID		forward_data(INT, INT);
static	PSOCKREQ	get_socks_request_from_socket(PTHREADINFO);
static	BOOL		got_cstring(PUCHAR, INT);
static	VOID		handle_connection(PVOID);
static	INT		open_requested_connection(PSOCKREQ);
static	VOID		run_error(PUCHAR, ...);
static	INT		send_socks_response(INT, UCHAR, USHORT, INADDR);
static	VOID		set_bind_address(PTHREADINFO, INT);

/* Local storage */

static	UCHAR		logmsg[MAXLOG];
static	volatile BOOL	shutting_down;


/*
 * This is the main server code.
 *
 * Returns:
 *	TRUE		server ran and terminated
 *	FALSE		server failed to start
 *
 */

INT server(PCONFIG config)
{	INT i, param, rc;
	INT ntint = config->ntinterfaces;
	PUCHAR temp;
	SOCK sa;
	PINADDRENT pinta;
	UCHAR ssa[MAXADDR];
	PINT listen_sockets = (PINT) malloc(config->ntinterfaces * sizeof(INT));
	PINT sockset = (PINT) malloc(config->ntinterfaces * sizeof(INT) * 2);

	if(listen_sockets == (PINT) NULL || sockset == (PINT) NULL) {
		run_error("failed to allocate memory");
		return(FALSE);
	}

	/* Set up listening sockets, one per trusted interface */

	pinta = config->tinterface_list;	/* Start of interface chain */
	for(i = 0; i < ntint; i++) {
		memset((PUCHAR) &sa, 0, sizeof(SOCK));
		sa.sin_family = AF_INET;
		sa.sin_port = config->port;
		sa.sin_addr = pinta->interface_address;
		pinta = pinta->next;		/* Move to next interface */
		strcpy(ssa, inet_ntoa(sa.sin_addr));

		listen_sockets[i] = socket(PF_INET, SOCK_STREAM, 0);
		if(listen_sockets[i] == -1) {
			run_error(
				"failed to allocate socket for interface %s"
				": rc = %d",
				ssa,
				sock_errno());
			return(FALSE);
		}
	
		param = 1;
		if((setsockopt(
			listen_sockets[i],
			SOL_SOCKET,
			SO_REUSEADDR,
			(PUCHAR) &param,
			sizeof(param))) == -1) {
			run_error(
				"failed to set socket for interface %s"
				" to reuse address: rc = %d",
				ssa,
				sock_errno());
			return(FALSE);
		}
	
		rc = bind(listen_sockets[i], (PSOCKG) &sa, sizeof(SOCK));
		if(rc == -1) {
			run_error(
				"failed to bind on interface %s, port %d: "
				"rc = %d",
				ssa,
				ntohs(sa.sin_port),
				sock_errno());
			return(FALSE);
		}

		sprintf(
			logmsg,
			"bound to interface %s, port %d",
			ssa,
			ntohs(sa.sin_port));
		dolog(logmsg);

		rc = listen(listen_sockets[i], LISTEN_BACKLOG);
		if(rc == -1) {
			run_error(
				"listen failed for interface %s, port %d: "
				"rc = %d",
				ssa,
				ntohs(sa.sin_port),
				sock_errno());
			return(FALSE);
		}
	}

	/* Set up signal handlers */

	signal(SIGTERM, catch_signal);
	signal(SIGBREAK, catch_signal);
	signal(SIGINT, catch_signal);

	/* Main listening loop */

	shutting_down = FALSE;

	while(shutting_down != TRUE) {

		/* Set up and perform select call */

#ifdef	DEBUG
		pinta = config->tinterface_list;
#endif
		for(i = 0; i < ntint; i++) {
			sockset[i] = listen_sockets[i];	/* Read sockets */
			sockset[i+ntint] = listen_sockets[i];	/* Exceptions */
#ifdef	DEBUG
			trace("added socket %d (interface %s) to"
				" select list",
				listen_sockets[i],
				inet_ntoa(pinta->interface_address));
			pinta = pinta->next;
#endif
		}

		if(select(sockset, ntint, 0, ntint, -1L) == -1) {
			if(sock_errno() != SOCEINTR) {
				run_error(
					"main select failed: rc = %d",
					sock_errno());
			}
			continue;
		}

		pinta = config->tinterface_list;

		for(i = 0; i < ntint; i++) {
			INADDR interface_address;
			UCHAR ias[MAXADDR];

			interface_address = pinta->interface_address;
			pinta = pinta->next;
			strcpy(ias, inet_ntoa(interface_address));

			if(sockset[i+ntint] != -1) {	/* Exception */
				sprintf(
					logmsg,
					"exception on socket %d for "
					"interface %s",
					listen_sockets[i],
					ias);
				dolog(logmsg);
				continue;
			}

			if(sockset[i] != -1) {		/* Read ready */
				SOCK csa;
				INT csockno;
				INT size = sizeof(SOCK);
				PTHREADINFO ti = (PTHREADINFO) malloc(sizeof(THREADINFO));

				if(ti == (PTHREADINFO) NULL) {
					run_error(
						"failed to allocate memory "
						"for thread data, socket %d",
						csockno);
					break;
				}
				ti->logmsg = malloc(MAXLOG);
				if(ti->logmsg == (PUCHAR) NULL) {
					run_error(
						"failed to allocate memory "
						"for thread log buffer, socket"
						" %d",
						csockno);
					free(ti);
					break;
				}

				csockno = accept(
						listen_sockets[i],
						(PSOCKG) &csa,
						&size);
				if(csockno == -1) {
					run_error("accept error on socket %d"
						  "for interface %s: rc = %d",
							listen_sockets[i],
							ias,
							sock_errno());
					continue;
				}
				sprintf(
					logmsg,
					"connection from %s on interface %s",
					inet_ntoa(csa.sin_addr),
					ias);
				dolog(logmsg);

				ti->config = config;
				ti->csockno = csockno;
				ti->ssockno = 0;	/* For now */
				ti->interface_address = interface_address;
				ti->client_address = csa.sin_addr;
				rc = _beginthread(
					handle_connection,
					NULL,
					THREAD_STACK,
					(PVOID) ti);
				if(rc == -1) {
					dolog("failed to create thread");
				}
				ti = (PTHREADINFO) NULL;
			}
		} /* select scan */
	} /* main loop */

	free((PUCHAR) listen_sockets);
	free((PUCHAR) sockset);

	pinta = config->tinterface_list;
	while(pinta != (PINADDRENT) NULL) {
		temp = (PUCHAR) pinta;
		pinta = pinta->next;
		free(temp);
	}
#ifdef	DEBUG
	_dump_allocated(24);
#endif

	dolog("shutdown complete");

	return(TRUE);
}


/*
 * Signal handler for the main listening thread.
 * Simply set shutdown flag and continue.
 *
 */

static VOID catch_signal(INT sig)
{	UCHAR msg[100];

	sprintf(
		msg,
		"%s detected, initiating shutdown",
		sig == SIGBREAK	? "Ctrl-Break" :
		sig == SIGINT	? "Ctrl-C" :
		sig == SIGTERM	? "Termination signal" :
				  "Unknown signal");
	dolog(msg);
	shutting_down = TRUE;
}


/*
 * Thread procedure to handle a connection. Must be completely re-entrant.
 * In fact, this procedure is merely a wrapper designed to ensure that all
 * resources are released before the thread terminates.
 *
 */

static VOID handle_connection(PVOID param)
{	PTHREADINFO ti = (PTHREADINFO) param;
	ti->request = (PSOCKREQ) NULL;

	do_handle_connection(ti);

	soclose(ti->csockno);
	if(ti->ssockno != 0) soclose(ti->ssockno);
	if(ti->request != (PSOCKREQ) NULL) {
		if(ti->request->userid != (PUCHAR) NULL)
			free(ti->request->userid);
		free(ti->request);
	}
	free(ti->logmsg);
	free(ti);
}


/*
 * The real thread procedure that handles a connection. Must be completely
 * re-entrant.
 *
 */

static VOID do_handle_connection(PTHREADINFO ti)
{	INT rc;
	INT bsockno;
	INT size;
	INT rsockno;
	SOCK bsa;
	SOCK rsa;
	PSOCKREQ request;
	INT sockset[1];
	UCHAR logbuf[MAXLOG];
	UCHAR ias[MAXADDR];
	UCHAR cas[MAXADDR];

	strcpy(ias, inet_ntoa(ti->interface_address));
	strcpy(cas, inet_ntoa(ti->client_address));

	request = get_socks_request_from_socket(ti);
	if(request == (PSOCKREQ) NULL) return;

	ti->request = request;	/* Save pointer so we can free request later */

	if(request->version != SOCKS_VERSION) {
		run_error("invalid version (%d) in SOCKS request on "
				"socket %d for interface %s",
				request->version,
				ti->csockno,
				ias);
		return;
	}

	switch(request->opcode) {
		case SOCKS_CONNECT:		/* Connect request */
			sprintf(logbuf,
				"user: %s, connect request to %s, port %d",
				request->userid,
				inet_ntoa(request->the_ip_addr),
				ntohs(request->port));
			dolog(logbuf);
	
			/* Open requested connection */

			ti->ssockno = open_requested_connection(request);
			if(ti->ssockno < 0) {
				/* Connection failed, so send client failed
				   response. If the notification fails, no big
				   deal, we are closing the client connection
				   in any case. */

				sprintf(
					logbuf,
					"connect failed for client socket %d "
					"(interface %s) from %s to %s, "
					"port %d; rc = %d",
					ti->csockno,
					ias,
					cas,
					inet_ntoa(request->the_ip_addr),
					ntohs(request->port),
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				break;
			}

			/* We are connected; send OK response, then
			   forward data */

			rc = send_socks_response(
				ti->csockno,
				SOCKS_RESP_GRANTED,
				request->port,
				request->the_ip_addr);

			if(rc < 0) {
				sprintf(
					logbuf,
					"send response to client failed on "
					"socket %d (interface %s) from %s to "
					"%s, port %d; rc = %d",
					ti->csockno,
					ias,
					cas,
					inet_ntoa(request->the_ip_addr),
					ntohs(request->port),
					sock_errno());
				dolog(logbuf);
			} else {
				forward_data(ti->csockno, ti->ssockno);
			}
			break;

		case SOCKS_BIND:		/* Bind request */
			sprintf(logbuf,
				"user: %s, bind request on socket %d"
				" (interface %s) from %s to %s, port %d",
				request->userid,
				ti->csockno,
				ias,
				cas,
				inet_ntoa(request->the_ip_addr),
				ntohs(request->port));
			dolog(logbuf);

			/* Check to see if bind is enabled */

			if(ti->config->bind_status == BIND_DISABLED) {
				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				sprintf(
					logbuf,
					"bind ignored (disabled) on socket %d "
					"(interface %s) from %s to %s, "
					"port %d",
					ti->csockno,
					ias,
					cas,
					inet_ntoa(request->the_ip_addr),
					ntohs(request->port));
				dolog(logbuf);
				break;
			}

			bsockno = socket(PF_INET, SOCK_STREAM, 0);
			if(bsockno == -1) {
				sprintf(
					logbuf,
					"failed to allocate bind socket for "
					"interface %s from %s, port %d; "
					"rc = %d",
					ias,
					cas,
					ntohs(request->port),
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				break;
			}

			/* Bind the socket to arbitrary port */

			set_bind_address(ti, bsockno);
			memset((PUCHAR) &bsa, 0, sizeof(SOCK));
			bsa.sin_family = AF_INET;
			bsa.sin_port = 0;
			bsa.sin_addr = ti->config->bind_address;

			rc = bind(bsockno, (PSOCKG) &bsa, sizeof(SOCK));
			if(rc == -1) {
				sprintf(
					logbuf,
					"failed to bind socket %d "
					"for interface %s from "
					"%s, port %d; rc = %d",
					bsockno,
					inet_ntoa(ti->config->bind_address),
					cas,
					ntohs(request->port),
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				soclose(bsockno);
				break;
			}

			/* Listen on bind socket */

			rc = listen(bsockno, 1);
			if(rc == -1) {
				sprintf(
					logbuf,
					"listen failed on bind socket %d"
					"for interface %s from "
					"%s, port %d; rc = %d",
					bsockno,
					inet_ntoa(ti->config->bind_address),
					cas,
					ntohs(request->port),
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				soclose(bsockno);
				break;
			}

			/* Tell client we are listening */

			size = sizeof(SOCK);
			getsockname(bsockno, (PSOCKG) &bsa, &size);

			sprintf(
				logbuf,
				"socket %d listening on %s, port %d",
				bsockno,
				inet_ntoa(bsa.sin_addr),
				ntohs(bsa.sin_port));
			dolog(logbuf);

			send_socks_response(
				ti->csockno,
				SOCKS_RESP_GRANTED,
				bsa.sin_port,
				bsa.sin_addr);

			/* Accept connection, with timeout */

			sockset[0] = bsockno;
			rc = select(sockset, 1, 0, 0, BIND_TIMEOUT*1000);
			if(rc == -1) {		/* Select failed */
				sprintf(
					logbuf,
					"select failed on socket %d: rc = %d",
					bsockno,
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				soclose(bsockno);
				break;
			}

			if(rc == 0) {		/* Listen timeout */
				sprintf(
					logbuf,
					"bind listen timeout on socket %d: "
					"rc = %d",
					bsockno,
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				soclose(bsockno);
				break;
			}

			/* We have an incoming connection; accept it */

			size = sizeof(SOCK);
			rsockno = accept(
					bsockno,
					(PSOCKG) &rsa,
					&size);
			soclose(bsockno);	/* No longer needed */

			if(rsockno == -1) {
				sprintf(
					logbuf,
					"accept failed on socket %d: "
					"rc = %d",
					bsockno,
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);
				break;
			}

			/* Check that connection is from whom we expect */

			if(request->the_ip_addr.s_addr != rsa.sin_addr.s_addr) {
				sprintf(
					logbuf,
					"bogus remote host %s accepted on "
					"socket %d: rc = %d",
					inet_ntoa(rsa.sin_addr),
					bsockno,
					sock_errno());
				dolog(logbuf);

				send_socks_response(
					ti->csockno,
					SOCKS_RESP_FAILED,
					request->port,
					request->the_ip_addr);

				soclose(rsockno);
				break;
			}

			/* Connection accepted from the correct host.
			   Now notify the client, then begin forwarding data */

			sprintf(
				logbuf,
				"inbound connection accepted from %s "
				"on socket %d",
				inet_ntoa(rsa.sin_addr),
				rsockno);
			dolog(logbuf);

			rc = send_socks_response(
					ti->csockno,
					SOCKS_RESP_GRANTED,
					request->port,
					request->the_ip_addr);
			if(rc < 0) {
				sprintf(
					logbuf,
					"send response to client %s failed",
					inet_ntoa(ti->client_address));
				dolog(logbuf);
				soclose(rsockno);
				break;
			}

			forward_data(ti->csockno, rsockno);
			soclose(rsockno);
			break;

		default:			/* Unrecognised opcode */
			sprintf(
				logbuf,
				"unrecognized opcode: (%d), closing connection",
				request->opcode);
			dolog(logbuf);
			break;
	}
}


/*
 * Set the bind address. This is a null operation if the config file
 * specified an IP address, but if it specified a device then the
 * corresponding address is extracted and set into the config structure.
 *
 * Any error causes the bind address to be set to INADDR_NONE.
 *
 */

static VOID set_bind_address(PTHREADINFO ti, INT sockno)
{	INT rc, i;
	IFCONF ifc;
	PIFREQ ifr;
	SOCK ppsa;
	UCHAR buf[sizeof(IFREQ)*MAXINTERFACES];

	/* If an IP address was specified in the configuration file,
	   there is nothing to do */

	if(ti->config->bind_status == BIND_ADDRESS) return;

	/* Otherwise, start by getting the interface configuration */

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;

	rc = ioctl(sockno, SIOCGIFCONF, (PUCHAR) &ifc, sizeof(ifc));
	if(rc < 0) {
		sprintf(
			ti->logmsg,
			"get interface configuration failed: rc = %d",
			sock_errno());
		dolog(ti->logmsg);
		soclose(sockno);
		ti->config->bind_address.s_addr = INADDR_NONE;
		return;
	}

	/* Search for the referral interface and determine if it is up */

	ifr = ifc.ifc_req;
	for(i = 0; i < ifc.ifc_len/sizeof(IFREQ); i++, ifr++) {

		/* Check that this interface is a TCP/IP one */

		if(ifr->ifr_addr.sa_family != AF_INET) continue;

		/* See if this is the interface we want */

		if(stricmp(ti->config->bind_device, ifr->ifr_name) != 0)
			continue;

		/* Now get the interface flags and see if it is up */

		rc = ioctl(
			sockno,
			SIOCGIFFLAGS,
			(PUCHAR) ifr,
			sizeof(IFREQ));
		if(rc < 0) {
			sprintf(
				ti->logmsg,
				"get interface flags for %s failed: rc = %d",
				ti->config->bind_device,
				sock_errno());
			dolog(ti->logmsg);
			break;
		}

		/* If interface is up, get the interface address */

		if((ifr->ifr_flags & IFF_UP) != 0) {
#ifdef	DEBUG
			trace("interface %s is up", ifr->ifr_name);
#endif
			rc = ioctl(
				sockno,
				SIOCGIFADDR,
				(PUCHAR) ifr,
				sizeof(IFREQ));
			if(rc < 0) {
				sprintf(
					ti->logmsg,
					"get interface address "
					"for %s failed: rc = %d",
					ti->config->bind_device,
					sock_errno());
				dolog(ti->logmsg);
				break;
			}
			memcpy(&ppsa, &ifr->ifr_dstaddr, sizeof(SOCK));
			ti->config->bind_address.s_addr = ppsa.sin_addr.s_addr;
#ifdef	DEBUG
			trace(
				"interface address for %s is %s",
				ti->config->bind_device,
				inet_ntoa(ppsa.sin_addr));
#endif
			return;
		}
#ifdef	DEBUG
		trace("interface %s is down", ifr->ifr_name);
#endif
		break;			/* No point in looking further */
	}

	ti->config->bind_address.s_addr = INADDR_NONE;	/* Indicate error */
}


/*
 * Read a SOCKS request from a socket. Returns pointer to structure
 * containing the request details, or NULL if there was an error.
 *
 */

static PSOCKREQ get_socks_request_from_socket(PTHREADINFO ti)
{	PSOCKREQ req = (PSOCKREQ) malloc(sizeof(SOCKREQ));
	UCHAR userid[MAXUSER+1];
	INT numread = 0;
	INT totalread = 0;
	UCHAR logbuf[MAXLOG];

	if(req == (PSOCKREQ) NULL) {
		sprintf(
			logbuf,
			"failed to allocate memory for SOCKS request on "
			"socket %d for interface %s",
			ti->csockno,
			inet_ntoa(ti->interface_address));
		dolog(logbuf);
		return((PSOCKREQ) NULL);
	}

	req->userid = (PUCHAR) NULL;

	/* Read the request header directly into the request structure */

	while(totalread != SOCKREQ_HDRSIZE) {
		numread = recv(
				ti->csockno,
				(PUCHAR) req + totalread,
				SOCKREQ_HDRSIZE - totalread,
				0);
		if(numread < 0) {
			sprintf(
				logbuf,
				"read SOCKS request on socket %d for "
				"interface %s failed in header: rc = %d",
				ti->csockno,
				inet_ntoa(ti->interface_address),
				sock_errno());
			dolog(logbuf);
			free(req);
			return((PSOCKREQ) NULL);
		}

		if(numread == 0) {
			sprintf(
				logbuf,
				"premature EOF reading SOCKS request on "
				"socket %d for interface %s: rc = %d",
				ti->csockno,
				inet_ntoa(ti->interface_address),
				sock_errno());
			dolog(logbuf);
			free(req);
			return((PSOCKREQ) NULL);
		}

		totalread += numread;
	}

	/* Now read the user ID and attach it to the request structure */

	totalread = 0;
	numread = 0;
	while((totalread < MAXUSER) && (got_cstring(userid, totalread)) == FALSE) {
		numread = recv(
				ti->csockno,
				userid + numread,
				MAXUSER - totalread,
				0);

		if(numread < 0)	{
			sprintf(
				logbuf,
				"read SOCKS request user ID on "
				"socket %d for interface %s failed: rc = %d",
				ti->csockno,
				inet_ntoa(ti->interface_address),
				sock_errno());
			dolog(logbuf);
			free(req);
			return((PSOCKREQ) NULL);
		}

		if(numread == 0) {
			sprintf(
				logbuf,
				"premature EOF reading SOCKS request user ID "
				"on socket %d for interface %s: rc = %d",
				ti->csockno,
				inet_ntoa(ti->interface_address),
				sock_errno());
			dolog(logbuf);
			free(req);
			return((PSOCKREQ) NULL);
		}

		totalread += numread;
	}

	req->userid = malloc(strlen(userid)+1);
	if(req->userid == (PUCHAR) NULL) {
		sprintf(
			logbuf,
			"failed to allocate memory for user ID for SOCKS "
			"request on socket %d for interface %s",
			ti->csockno,
			inet_ntoa(ti->interface_address));
		dolog(logbuf);
		free(req);
		return((PSOCKREQ) NULL);
	}
	strcpy(req->userid, userid);

	return(req);
}


/*
 * Check whether 'buf' contains a valid, null-terminated C-style string.
 *
 */

static BOOL got_cstring(PUCHAR buf, INT length)
{	int i;

	for(i = 0; i < length; i++)
		if(buf[i] == 0) return(TRUE);

	return(FALSE);
}


/*
 * Send a SOCKS response to a socket.
 *
 * Returns 0 for success, -1 for failure.
 *
 */

static INT send_socks_response(INT socket, UCHAR opcode, USHORT port,
					INADDR addr)
{	SOCKRESP resp;
	INT totalwritten = 0;
	INT numwritten = 0;
	INT size = sizeof(SOCKRESP);

	resp.version = 0;
	resp.opcode = opcode;
	resp.port = port;
	resp.the_ip_addr = addr;

	while(totalwritten < size) {
		numwritten = send(
				socket,
				(PUCHAR) &resp + totalwritten,
				size - totalwritten,
				0);
		if(numwritten < 0) return(-1);

		totalwritten += numwritten;
	}

	return(0);
}


/*
 * Open a real connection based on what the client requested.
 *
 * Returns the number of the socket for the connection, or -1 if there has
 * been an error.
 *
 */

static INT open_requested_connection(PSOCKREQ request)
{	INT sockno;
	INT rc;
	SOCK ssa;

	/* Get a new socket to make the connection */

	sockno = socket(PF_INET, SOCK_STREAM, 0);
	if(sockno < 0) {
		run_error("failed to allocate socket for address %s",
				": rc = %d",
				inet_ntoa(request->the_ip_addr),
				sock_errno());
		return(-1);
	}

	/* Set up the address */

	memset((PUCHAR) &ssa, 0, sizeof(SOCK));
	ssa.sin_family = AF_INET;
	ssa.sin_port = request->port;
	ssa.sin_addr = request->the_ip_addr;

	/* Attempt to connect */

	rc = connect(sockno, (PSOCKG) &ssa, sizeof(SOCK));
	if(rc < 0) {
		soclose(sockno);
		return(-1);
	}

#ifdef	DEBUG
	trace(
		"connected with socket %d to %s, port %d",
		sockno,
		inet_ntoa(ssa.sin_addr),
		ntohs(ssa.sin_port));
#endif

        return(sockno);
}


/*
 * Forward data bidirectionally between two sockets.
 *
 */

static VOID forward_data(INT csockno, INT ssockno)
{	UCHAR buf[IOBUFSIZE];
	BOOL done = FALSE;
	INT rc;
	INT sockset[2];
	INT numread, numwritten;

#ifdef	DEBUG
	trace("forwarding data: %d <==> %d", csockno, ssockno);
#endif

	while(done != TRUE) {
		sockset[0] = csockno;
		sockset[1] = ssockno;

		rc = select(sockset, 2, 0, 0, READ_TIMEOUT*1000);
		if(rc == -1) {
			run_error(
				"error on select for sockets %d and %d: "
				"rc = %d",
				csockno,
				ssockno,
				sock_errno());
			done = TRUE;
			continue;
		}

		if(rc == 0) {		/* Read timeout */
			sprintf(
				buf,
				"read timeout for sockets %d and %d",
				csockno,
				ssockno);
			dolog(buf);
			done = TRUE;
			continue;
		}

		if(sockset[1] != -1) {		/* ssockno has traffic */
			numread = recv(ssockno, buf, IOBUFSIZE, 0);
			if(numread > 0)	{
				numwritten = send(csockno, buf, numread, 0);
				if(numwritten != numread) {
					run_error(
						"write error to socket %d: "
						"rc = %d",
						csockno,
						sock_errno());
					done = TRUE;
				}
			} else {
				if(numread < 0)	{
					run_error(
						"read error on socket %d: "
						"rc = %d",
						ssockno,
						sock_errno());
					done = TRUE;
				} else {	/* numread == 0 */
					/* EOF on ssockno - server has
					   closed the connection */
					done = TRUE;
				}
			}
		} /* ssockno traffic */

		if(done == TRUE) continue;

		if(sockset[0] != -1) {		/* csockno has traffic */
			numread = recv(csockno, buf, IOBUFSIZE, 0);
			if(numread > 0)	{
				numwritten = send(ssockno, buf, numread, 0);
				if(numwritten != numread) {
					run_error(
						"write error to socket %d: "
						"rc = %d",
						ssockno,
						sock_errno());
					done = TRUE;
				}
			} else {
				if(numread < 0)	{
					run_error(
						"read error on socket %d: "
						"rc = %d",
						csockno,
						sock_errno());
					done = TRUE;
				} else {	/* numread == 0 */
					/* EOF on csockno - client has
					   closed the connection */
					done = TRUE;
				}
			}
		} /* csockno traffic */
	} /* while loop */
}


/*
 * Output runtime error message to standard error in printf style,
 * with a copy to the logfile.
 *
 */

static void run_error(char *mes, ...)
{	va_list ap;
	char buf[MAXLOG];

	va_start(ap, mes);
	vsprintf(buf, mes, ap);
	va_end(ap);

	error(buf);
	dolog(buf);
}

/*
 * End of file: server.c
 *
 */
