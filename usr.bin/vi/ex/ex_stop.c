/*	$OpenBSD: ex_stop.c,v 1.6 2014/11/12 04:28:41 bentley Exp $	*/

/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../common/common.h"
#include "../cl/cl.h"

/*
 * ex_stop -- :stop[!]
 *	      :suspend[!]
 *	Suspend execution.
 */
int
ex_stop(SCR *sp, EXCMD *cmdp)
{
	int allowed;

	/* For some strange reason, the force flag turns off autowrite. */
	if (!FL_ISSET(cmdp->iflags, E_C_FORCE) && file_aw(sp, FS_ALL))
		return (1);

	if (cl_suspend(sp, &allowed))
		return (1);
	if (!allowed)
		ex_emsg(sp, NULL, EXM_NOSUSPEND);
	return (0);
}
