/*
 * Copyright 2021, Sine Nomine Associates and others.
 * All Rights Reserved.
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

#include <afsconfig.h>
#include <afs/param.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <afs/afsutil.h>
#include <tests/tap/basic.h>


/*
 * afs_getline() is usually used in a loop, like such:
 *
 *     example(void)
 *     {
 *         char *p = NULL;
 *         ssize_t len;
 *         size_t n = 0;
 *
 *         while ((len = afs_getline(&p, &n, stdin)) != -1)
 *             (void)printf("%zd %s", len, p);
 *         free(p);
 *     }
 */

int
main(int argc, char **argv)
{
    int code;
    int fd;
    char *filename = NULL;
    FILE *fp = NULL;
    char *line = NULL;
    size_t bufsize = 0;
    ssize_t length;
    int i;

    plan(9);

    /* Create a test file with some lines to read. */
    code = asprintf(&filename, "/tmp/afs_getline_XXXXXX");
    if (code < 0)
	sysbail("out of memory");

    fd = mkstemp(filename);
    if (fd < 0)
	sysbail("mkstemp");

    code = unlink(filename);
    if (code < 0)
	sysbail("unlink");

    fp = fdopen(fd, "w+");
    if (fp == NULL)
	sysbail("fdopen");

    fprintf(fp, "hello world\n");
    fprintf(fp, "\n"); /* empty */
    fprintf(fp, "a very long line: ");
    for (i = 0; i<1024; i++)
	fprintf(fp, "%s", "1234567890");
    fprintf(fp, "\n");
    fprintf(fp, "last\n");
    fseek(fp, 0, SEEK_SET); /* Seek to start for getline tests. */

    /* afs_getline tests. */
    length = afs_getline(&line, &bufsize, fp);
    is_int(length, 12, "test line length is ok");
    is_string(line, "hello world\n", "test line matches");

    length = afs_getline(&line, &bufsize, fp);
    is_int(length, 1, "empty length is ok");
    is_string(line, "\n", "empty line matches");

    length = afs_getline(&line, &bufsize, fp);
    is_int(length, 10259, "long line length is ok");
    if (length < 40)
	skip("did not read long line");
    else {
	line[40] = '\0';
	is_string(line, "a very long line: 1234567890123456789012",
		  "start of long line ok");
    }

    length = afs_getline(&line, &bufsize, fp);
    is_int(length, 5, "last line length is ok");
    is_string(line, "last\n", "empty line matches");

    length = afs_getline(&line, &bufsize, fp);
    is_int(length, -1, "end of file found");

    /* Cleanup */
    fclose(fp);
    free(line);
    free(filename);
    return 0;
}
