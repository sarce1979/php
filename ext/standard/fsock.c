/*
   +----------------------------------------------------------------------+
   | PHP Version 4                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2002 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.02 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available at through the world-wide-web at                           |
   | http://www.php.net/license/2_02.txt.                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Paul Panotzki - Bunyip Information Systems                  |
   |          Jim Winstead <jimw@php.net>                                 |
   |          Sascha Schumann <sascha@schumann.cx>                        |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

/* converted to PHP Streams and moved much code to main/network.c [wez] */

/* Synced with php 3.0 revision 1.121 1999-06-18 [ssb] */
/* Synced with php 3.0 revision 1.133 1999-07-21 [sas] */

#include "php.h"
#include "php_globals.h"
#include <stdlib.h>
#include <stddef.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef PHP_WIN32
#include <winsock.h>
#else
#include <netinet/in.h>
#include <netdb.h>
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#endif
#if defined(PHP_WIN32) || defined(__riscos__)
#undef AF_UNIX
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifndef PF_INET
#define PF_INET AF_INET
#endif

#ifndef PF_UNIX
#define PF_UNIX AF_UNIX
#endif

#include <string.h>
#include <errno.h>

#include "base64.h"
#include "file.h"
#include "url.h"
#include "fsock.h"

#include "php_network.h"

#ifdef ZTS
static int fsock_globals_id;
#else
extern int le_fp;
#endif

#ifdef PHP_WIN32
#define EWOULDBLOCK WSAEWOULDBLOCK
#else
#include "build-defs.h"
#endif

/* {{{ php_lookup_hostname */

/*
 * Converts a host name to an IP address.
 */
PHPAPI int php_lookup_hostname(const char *addr, struct in_addr *in)
{
	struct hostent *host_info;

	if (!inet_aton(addr, in)) {
		/* XXX NOT THREAD SAFE */
		host_info = gethostbyname(addr);
		if (host_info == 0) {
			/* Error: unknown host */
			return -1;
		}
		*in = *((struct in_addr *) host_info->h_addr);
	}
	return 0;
}
/* }}} */

/* {{{ php_fsockopen() */

static void php_fsockopen_stream(INTERNAL_FUNCTION_PARAMETERS, int persistent)
{
	char * host;
	int host_len;
	int port = -1;
	zval * zerrno = NULL, * zerrstr = NULL;
	double timeout = 60;
	unsigned long conv;
	struct timeval tv;
	char * hashkey = NULL;
	php_stream * stream = NULL;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|lzzd", &host, &host_len, &port, &zerrno, &zerrstr, &timeout) == FAILURE)	{
		RETURN_FALSE;
	}

	hashkey = emalloc(host_len + 10);
	sprintf(hashkey, "%s:%d", host, port);

	if (persistent && zend_hash_find(&FG(ht_persistent_socks), hashkey, strlen(hashkey) + 1,
				(void *) &stream) == SUCCESS)
	{
		efree(hashkey);
		ZEND_REGISTER_RESOURCE(return_value, stream, php_file_le_stream());
		return;
	}

	/* prepare the timeout value for use */
	conv = (unsigned long) (timeout * 1000000.0);
	tv.tv_sec = conv / 1000000;
	tv.tv_usec = conv % 1000000;

	if (zerrno)	{
		zval_dtor(zerrno);
		ZVAL_LONG(zerrno, 0);
	}
	if (zerrstr)	{
		zval_dtor(zerrstr);
		ZVAL_STRING(zerrno, "", 1);
	}

	if (port != -1)	{ /* connect to a host */
		enum php_sslflags_t { php_ssl_none, php_ssl_v23, php_ssl_tls };
		enum php_sslflags_t ssl_flags;
		struct {
			char * proto;
			int protolen;
			int socktype;
			enum php_sslflags_t ssl_flags;
			/* more flags to be added here */
		} sockmodes[] = {
			{ "udp://", 6, SOCK_DGRAM,	php_ssl_none },
			{ "tcp://", 6, SOCK_STREAM,	php_ssl_none },
			{ "ssl://", 6, SOCK_STREAM, php_ssl_v23 },
			{ "tls://", 6, SOCK_STREAM, php_ssl_tls },
			/* more modes to be added here */
			{ NULL, 0, 0 }
		};
		int socktype = SOCK_STREAM;
		int i;

		for (i = 0; sockmodes[i].proto != NULL; i++)	{
			if (strncmp(host, sockmodes[i].proto, sockmodes[i].protolen) == 0)	{
				ssl_flags = sockmodes[i].ssl_flags;		
				socktype = sockmodes[i].socktype;
				host += sockmodes[i].protolen;
				break;
			}
		}
#if !HAVE_OPENSSL_EXT
		if (ssl_flags != php_ssl_none)	{
			zend_error(E_WARNING, "%s(): no SSL support in this build", get_active_function_name(TSRMLS_C));
		}
		else
#endif
		stream = php_stream_sock_open_host(host, port, socktype, timeout, persistent);

#if HAVE_OPENSSL_EXT
		if (stream)	{
			int ssl_ret = FAILURE;
			switch(ssl_flags)	{
				case php_ssl_v23:
					ssl_ret = php_stream_sock_ssl_activate_with_method(stream, 1, SSLv23_client_method());
					break;
				case php_ssl_tls:
					ssl_ret = php_stream_sock_ssl_activate_with_method(stream, 1, TLSv1_client_method());
					break;
				default:
					/* unknown ?? */
			}
			if (ssl_ret == FAILURE)
				zend_error(E_WARNING, "%s(): failed to activate SSL mode %d", get_active_function_name(TSRMLS_C), ssl_flags);
		}
#endif
		
	} else	
		stream = php_stream_sock_open_unix(host, persistent, &tv);

	if (stream && persistent)	{
		zend_hash_update(&FG(ht_persistent_socks), hashkey, strlen(hashkey) + 1,
				&stream, sizeof(stream), NULL);
	}

	efree(hashkey);
	
	if (stream == NULL)	{
		if (zerrno)	{
			zval_dtor(zerrno);
			ZVAL_LONG(zerrno, errno);
		}
		if (zerrstr)	{
			zval_dtor(zerrstr);
			ZVAL_STRING(zerrno, strerror(errno), 1);
		}

		RETURN_FALSE;
	}
		
	ZEND_REGISTER_RESOURCE(return_value, stream, php_file_le_stream());
}

/* }}} */

/* {{{ proto int fsockopen(string hostname, int port [, int errno [, string errstr [, float timeout]]])
   Open Internet or Unix domain socket connection */
PHP_FUNCTION(fsockopen)
{
	php_fsockopen_stream(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */
/* {{{ proto int pfsockopen(string hostname, int port [, int errno [, string errstr [, float timeout]]])
   Open persistent Internet or Unix domain socket connection */
PHP_FUNCTION(pfsockopen)
{
	php_fsockopen_stream(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */

/* {{{ RSHUTDOWN_FUNCTION(fsock) */
PHP_RSHUTDOWN_FUNCTION(fsock)
{
	return SUCCESS;
}
/* }}} */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
