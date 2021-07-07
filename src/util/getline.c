/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Implementations of the getdelim() and getline() functions as specified by
 * POSIX 2008 in case they are not available on your platform.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/**
 * Read an entire line from a stream using the given delimiter
 *
 * getdelim() works like getline(), except that a line delimiter
 * other than newline can be specified as the delimiter argument.
 *
 * @param  buf         output buffer
 * @param  bufsize     output buffer size
 * @param  delimiter   end of line delimiter
 * @param  fp          stream opened for read
 *
 * @returns Number of bytes read, including the delimiter, excluding the terminating
 *          nul character, or -1 to indicate an error.
 */
ssize_t
afs_getdelim(char **buf, size_t *bufsiz, int delimiter, FILE *fp)
{
    char *ptr, *eptr;

    if (*buf == NULL || *bufsiz == 0) {
	*bufsiz = BUFSIZ;
	if ((*buf = malloc(*bufsiz)) == NULL)
	    return -1;
    }

    for (ptr = *buf, eptr = *buf + *bufsiz;;) {
	int c = fgetc(fp);
	if (c == -1) {
	    if (feof(fp)) {
		ssize_t diff = (ssize_t) (ptr - *buf);
		if (diff != 0) {
		    *ptr = '\0';
		    return diff;
		}
	    }
	    return -1;
	}
	*ptr++ = c;
	if (c == delimiter) {
	    *ptr = '\0';
	    return ptr - *buf;
	}
	if (ptr + 2 >= eptr) {
	    char *nbuf;
	    size_t nbufsiz = *bufsiz * 2;
	    ssize_t d = ptr - *buf;
	    if ((nbuf = realloc(*buf, nbufsiz)) == NULL)
		return -1;
	    *buf = nbuf;
	    *bufsiz = nbufsiz;
	    eptr = nbuf + nbufsiz;
	    ptr = nbuf + d;
	}
    }
}

/*
 * Read an entire line from a stream
 *
 * getline() reads an entire line from the stream up to and including a newline
 * character. The output will be nul terminated.  The buffer will be
 * reallocated as needed if it is not large enough to hold the line. The caller
 * must free the output buffer.
 *
 * A a delimiter character will be present if one was reached while reading
 * from the stram. A delimiter character is not added if one was not present in
 * the input before end of file was reached.
 *
 * @param  buf         output buffer
 * @param  bufsize     output buffer size
 * @param  delimiter   end of line delimiter
 * @param  fp          stream opened for read
 *
 * @returns Number of bytes read, including the newline, excluding the terminating
 *          nul character, or -1 to indicate an error.
 */
ssize_t
afs_getline(char **buf, size_t *bufsiz, FILE * fp)
{
    return afs_getdelim(buf, bufsiz, '\n', fp);
}
