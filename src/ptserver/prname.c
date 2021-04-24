/*
 * Copyright (c) 2021 Sine Nomine Associates. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Set of functions used to manipulate prnames */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>
#include <ctype.h>

#include "prname.h"

/**
 * Check if user or group is blank.
 *
 * @param[in] aname  user or group
 * @param[in] asize  size of aname
 *
 * @return 1 if aname is blank; 0 otherwise.
 *
 * @note Since array lengths in function signatures are not enforced by the
 *       compiler, we chose to receive a regular string and its size as
 *       arguments (instead of a prname).
 */
int
pr_IsNameBlank(char *aname, size_t asize)
{
    int code;
    char *name, *last;
    char *atsign, *suffix;

    if (aname == NULL || asize <= 0) {
	return 1;
    }
    code = 0;
    name = aname;
    last = &aname[asize - 1] + 1;

    /* remove cell, if present */
    atsign = memchr(name, '@', asize);
    if (atsign != NULL) {
	last = atsign;
	asize = atsign - name;
    }
    /* remove owner, if present */
    suffix = memchr(name, ':', asize);
    if (suffix != NULL && suffix < last) {
	name = suffix + 1;
    }

    /* check if user or group is blank */
    while ((name < last) && isspace(*name)) {
	name++;
    }
    if ((name >= last) || (*name == '\0')) {
	code = 1;
    }
    return code;
}
