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
 */

#include <config.h>

#if ! HAVE_VASPRINTF
# if HAVE_STDARG_H
#  include <stdarg.h>
# endif

int		asprintf    __P((char **, char *, ...));
int		vasprintf   __P((char **, char *, ...));
#endif
