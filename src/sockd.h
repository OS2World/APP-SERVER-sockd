/*
 * File: sockd.h
 *
 * SOCKS daemon for OS/2.
 *
 * Header file
 *
 * Bob Eager   October 1997
 *
 */

#include <os2.h>

#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define	OS2
#include <netdb.h>
#include <types.h>
#include <utils.h>
#include <netinet\in.h>
#include <sys\socket.h>
#include <sys\ioctl.h>
#include <net\if.h>
#include <arpa\nameser.h>
#include <resolv.h>
#include <nerrno.h>

#define	VERSION			1	/* Major version number */
#define	EDIT			0	/* Edit number within major version */

#define	FALSE			0
#define	TRUE			1

/* Configuration constants */

#define	MAXADDR			16	/* Size of buffer to hold dotted IP address */
#define	MAXINTERFACES		15	/* Maximum number of interfaces */
#define	MAXLOG			200	/* Maximum length of a logfile line */
#define	LISTEN_BACKLOG		5	/* Queue length for pending connections */

/* Bind status values */

#define	BIND_DISABLED		0	/* Bind status - disabled */
#define	BIND_ADDRESS		1	/* Bind status - address */
#define	BIND_INTERFACE		2	/* Bind status - interface name */

/* SOCKS protocol constants and opcodes */

#define	SOCKS_VERSION		4	/* Protocol version */

#define	SOCKS_CONNECT		1	/* Connect opcode */
#define	SOCKS_BIND		2	/* Bind opcode */

#define	SOCKS_RESP_GRANTED	90	/* Request granted */
#define	SOCKS_RESP_FAILED	91	/* Request rejected or failed */
#define	SOCKS_RESP_NOIDENTD	92	/* Cannot connect to IDENTD on client */
#define	SOCKS_RESP_BADUSER	93	/* User IDs do not match */

/* Type definitions */

typedef	struct ifconf		IFCONF, *PIFCONF;	/* Interface configuration */
typedef	struct ifreq		IFREQ, *PIFREQ;		/* Interface information */
typedef struct in_addr		INADDR, *PINADDR;	/* Internet address */
typedef	struct servent		SERV, *PSERV;		/* Service structure */
typedef	struct sockaddr		SOCKG, *PSOCKG;		/* Generic structure */
typedef	struct sockaddr_in	SOCK, *PSOCK;		/* Internet structure */

/* Structure definitions */

typedef struct _INADDRENT {		/* Interface address list entry */
struct _INADDRENT	*next;
INADDR			interface_address;
} INADDRENT, *PINADDRENT;

typedef struct _CONFIG {		/* Configuration information */
INT		ntinterfaces;		/* Number of trusted interfaces */
PINADDRENT	tinterface_list;	/* Trusted interface list */
INADDR		bind_address;		/* Un-trusted interface address for binds */
UCHAR		bind_device[IFNAMSIZ];	/* Un-trusted interface name */
BOOL		bind_status;		/* Status of bind interface */
USHORT		port;			/* Port to listen on */
} CONFIG, *PCONFIG;

typedef struct _SOCKREQ {		/* SOCKS request */
UCHAR		version;		/* SOCKS version */
UCHAR		opcode;			/* Operation requested */
USHORT		port;			/* Destination port */
INADDR		the_ip_addr;		/* Destination IP address */
PUCHAR		userid;			/* Pointer to user ID string */
} SOCKREQ, *PSOCKREQ;

#define	SOCKREQ_HDRSIZE	(sizeof(UCHAR)+sizeof(UCHAR)+sizeof(USHORT)+sizeof(INADDR))

typedef struct _SOCKRESP {		/* SOCKS response */
UCHAR		version;		/* Response code version */
UCHAR		opcode;			/* Response code */
USHORT		port;			/* Transaction port */
INADDR		the_ip_addr;		/* Transaction IP address */
} SOCKRESP, *PSOCKRESP;

typedef struct _THREADINFO {		/* Thread information */
PCONFIG		config;			/* Configuration information */
INT		csockno;		/* Client socket number */
INT		ssockno;		/* Server socket number */
INADDR		interface_address;	/* Interface used by client socket */
INADDR		client_address;		/* Address of client system */
PSOCKREQ	request;		/* Pointer to request structure */
PUCHAR		logmsg;			/* Pointer to log buffer */
} THREADINFO, *PTHREADINFO;

/* External references */

extern	VOID	error(PUCHAR, ...);
extern	INT	read_config(PUCHAR, PUCHAR, PCONFIG);
extern	INT	server(PCONFIG);

/*
 * End of file: sockd.h
 *
 */
