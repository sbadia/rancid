/*
 * Copyright (C) 1997-2002 by Henry Kilmer, Erik Sherk and Pete Whiting.
 * All rights reserved.
 *
 * This software may be freely copied, modified and redistributed without
 * fee for non-commerical purposes provided that this copyright notice is
 * preserved intact on all copies and modified copies.
 *
 * There is no warranty or other guarantee of fitness of this software.
 * It is provided solely "as is". The author(s) disclaim(s) all
 * responsibility and liability with respect to this software's usage
 * or its effect upon hardware, computer systems, other software, or
 * anything else.
 *
 *
 * support routines
 *
 */

#include <config.h>
#include <util.h>
#include <stdio.h>
#include <varargs.h>

/*
 * function:	mktmp()
 *  returns:
 * synopsis:	creates a temporary file.
 */

/*
 * function:	pidfile()
 *  returns:
 * synopsis:	creates a PID file.
 */

/*
 * function:	sm_close()
 *  returns:
 * synopsis:	closes sendmail pipe opened by sm_open().
 */

/*
 * function:	sm_open()
 *  returns:
 * synopsis:	open sendmail vforks and starts sendmail.
 */

#if 0
#include <time.h>

#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <regex.h>

#include <termios.h>
#endif

#if ! HAVE_VASPRINTF
/*
 * function:	int asprintf()
 *  returns:	non-zero on malloc failure
 * synopsis:	emulate asprintf() for platforms without
 */
int
#ifdef __STDC__
asprintf(char **ret, char *format, ...)
#else
asprintf(ret, format, ...)
    char	**ret;
    char	*format;
    /*va_dcl*/
#endif
{
    va_list	ap;
    int		len,
		vlen;

    if (ret == NULL)
	return(-1);

    if (format == NULL) {
	*ret = NULL;
	return(0);
    }

    len = strlen(format) + 1;
    if (ap != NULL)
	len = len * 2;

    if ((*ret = (char *) xmalloc(len)) == NULL)
	    return(-1);

    while (1) {
	va_start(ap, format);
	if ((vlen = vsnprintf(*ret, len, format, ap)) <= len) {
	    va_end(ap);
	    return(vlen);
	}
	va_end(ap);
	len = vlen + 1;
	if (xrealloc((void *)ret, len) == NULL) {
	    free(*ret);
	    *ret = NULL;
	    return(-1);
	}
    }

    return(0);
}
#endif

#if ! HAVE_VASPRINTF
/*
 * function:	int vasprintf()
 *  returns:	non-zero on failure
 * synopsis:	emulate vasprintf() for platforms without
 */
int
#ifdef __STDC__
vasprintf(char **ret, char *format, ...)
#else
vasprintf(ret, format, ap)
    char	**ret;
    char	*format;
    va_list	ap;
#endif
{
    va_list	ap;
    int		len,
		vlen;

    if (ret == NULL)
	return(-1);

    if (format == NULL) {
	*ret = NULL;
	return(0);
    }

    len = strlen(format) + 1;
    if (ap != NULL)
	len = len * 2;

    if ((*ret = (char *) xmalloc(len)) == NULL)
	    return(-1);

    while (1) {
	if ((vlen = vsnprintf(*ret, len, format, ap)) < len) {
	    return(vlen);
	}
	len = vlen + 1;
	if (xrealloc((void *)ret, len) == NULL) {
	    free(*ret);
	    *ret = NULL;
	    return(-1);
	}
    }

    return(0);
}
#endif

#if ! HAVE_VASPRINTF
/*
 *  Subroutine:	xmalloc
 *       Usage:	void * xmalloc(size)
 * Description:	retuns ptr to zero'd block of size bytes, else logs msg
 *		and returns null
 */
void *
xmalloc(size)
    int		size;
{
    void	*ptr;

    if (size <= 0)
	return(NULL);

    if ((ptr = malloc(size)) == NULL)
	return(NULL);

    /* zero the space */
    bzero(ptr, size);

    return(ptr);
}

/*
 *  Subroutine:	xrealloc
 *       Usage:	void * xrealloc(void *ptr, int size)
 * Description:	adjusts ptr to size bytes, else logs msg and returns null
 *     returns:	returns NULL on malloc error and leaves ptr alone
 */
void *
xrealloc(ptr, size)
    void	**ptr;
    int		size;
{
    void	*newptr;

    if (debug & DEBUG_CALL)
	debug_msg("xrealloc(%d)\n", size);

    if (ptr == NULL || size <= 0)
	return(NULL);

    if (*ptr == NULL) {
	if ((newptr = xmalloc(size)) != NULL) {
	    *ptr = newptr;
	    return(newptr);
	} else
	    return(NULL);
    }

    if ((newptr = realloc(*ptr, size)) == NULL)
	return(NULL);

    *ptr = newptr;

    return(newptr);
}
#endif
