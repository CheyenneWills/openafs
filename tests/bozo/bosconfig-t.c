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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <afs/afsutil.h>
#include <afs/bosint.h>
#include <afs/bnode.h>
#include <bnode_internal.h> /* Required for bosprotoypes.h */
#include <bosprototypes.h>
#include <tests/tap/basic.h>

#include "mock-bnode.h"

static int verbose = 0;
static char *last_log = NULL;

/* Globals serialized from and to the BosConfig file. */
struct ktime bozo_nextRestartKT;
struct ktime bozo_nextDayKT;
int bozo_isrestricted;

/* Stubs to satisfy bosconfig.c and bnode.c dependencies. */
int bozo_restdisable = 0;
char *DoCore = NULL;
void * bozo_ShutdownAndExit(void *param) { return NULL; }
int osi_audit(void ) { return 0; }

/**
 * Capture the most recent log message for tests.
 */
void
bozo_Log(const char *format, ...)
{
    va_list ap;

    free(last_log);
    last_log = NULL;
    va_start(ap, format);
    if (vasprintf(&last_log, format, ap) < 0)
	sysbail("out of memory");
    va_end(ap);
    if (verbose)
	diag("BosLog: %s", last_log);
}

/**
 * Create a BosConfig test file with the given contents.
 * Returns the filename in pfilename, which must be freed by
 * the caller.
 */
void
create_file(char **pfilename, const char *text)
{
    int code;
    char *filename = NULL;
    int fd;
    FILE *fp;

    code = asprintf(&filename, "/tmp/afs_bosconfig_XXXXXX");
    if (code < 0)
	sysbail("out of memory");
    fd = mkstemp(filename);
    if (fd < 0)
	sysbail("mkstemp");
    fp = fdopen(fd, "w");
    if (fp == NULL)
	sysbail("failed to open file %s", filename);
    code = fprintf(fp, "%s", text);
    if (code < 0)
	sysbail("failed to write file %s", filename);
    code = fclose(fp); /* Closes fd too. */
    if (code < 0)
	sysbail("failed close file %s", filename);
    *pfilename = filename;
}

/**
 * Write contents to a file.
 */
void
write_file(char *filename, char *text)
{
    int code;
    FILE *fp;

    fp = fopen(filename, "w");
    if (fp == NULL)
	sysbail("failed to open file %s", filename);
    code = fprintf(fp, "%s", text);
    if (code < 0)
	sysbail("failed to write file %s", filename);
    code = fclose(fp);
    if (code < 0)
	sysbail("failed close file %s", filename);
}

/**
 * Read the contents of a file.
 */
char *
read_file(const char *filename)
{
    int code;
    FILE *fp;
    struct stat st;
    char *contents;

    fp = fopen(filename, "r");
    if (fp == NULL)
	sysbail("failed to open file %s", filename);
    code = fstat(fileno(fp), &st);
    if (code < 0)
	sysbail("failed to stat file %s", filename);
    contents = bcalloc(st.st_size + 1, sizeof(char)); /* Bails on failure. */
    if (fread(contents, 1, st.st_size, fp) != st.st_size)
	sysbail("failed to read file %s", filename);
    code = fclose(fp);
    if (code < 0)
	sysbail("failed to close file %s", filename);
    return contents;
}

/**
 * Verify the last log entry contains the given string.
 */
void
is_log_contains(char *text)
{
    ok(last_log != NULL && strstr(last_log, text) != NULL,
       "log message contains '%s'", text);
}

/**
 * Compare file contents.
 */
void
is_file_contents(char *file, char *expect, char *msg)
{
    char *contents = read_file(file);
    is_string(contents, expect, "%s", msg);
    free(contents);
}

/**
 * Compare ktime values.
 */
void
is_ktime(struct ktime *t, int mask, short day, short hour,
	 short min, short sec, const char *msg)
{
    is_int(t->mask, mask, "%s: mask", msg);
    is_int(t->day, day, "%s: day", msg);
    is_int(t->hour, hour, "%s: hour", msg);
    is_int(t->min, min, "%s: min", msg);
    is_int(t->sec, sec, "%s: sec", msg);
}

/**
 * Check the number of bnodes.
 */
void
is_bnode_count(int count)
{
    is_int(mock_bnode_count(), count, ".. number of bnodes is %d", count);
}

/**
 * Compare mock bnode values of the given bnode index.
 */
void
is_bnode(int index, char *type, char *name, int status,
	 char *a0, char *a1, char *a2, char *a3, char *a4, char *notifier)
{
    struct mock_bnode *b;

    b = mock_bnode_find(index);
    ok(b != NULL, ".. bnode %d: found", index);
    if (b == NULL) {
	skip_block(7, ".. bnode checks; bnode %d not found", index);
    } else {
	is_string(b->b.type->name, type, ".. bnode %d: type", index);
	is_string(b->name, name, ".. bnode %d: name", index);
	is_int(b->status, status, ".. bnode %d: status", index);
	is_string(b->args[0], a0, ".. bnode %d: arg 0", index);
	is_string(b->args[1], a1, ".. bnode %d: arg 1", index);
	is_string(b->args[2], a2, ".. bnode %d: arg 2", index);
	is_string(b->args[3], a3, ".. bnode %d: arg 3", index);
	is_string(b->args[4], a4, ".. bnode %d: arg 4", index);
	is_string(b->b.notifier, notifier, ".. bnode %d: notifier", index);
    }
}

/*
 * Test setup and teardown boilerplates.
 */
#define READ_TEST_SETUP(name, code, config) \
    do { \
	test_name = name; \
	test_code = code; \
	create_file(&test_file, config); \
	bozo_isrestricted = -1; \
	memset(&bozo_nextRestartKT, -1, sizeof(bozo_nextRestartKT)); \
	memset(&bozo_nextDayKT, -1, sizeof(bozo_nextDayKT)); \
	mock_bnode_free(); \
    } while (0)

#define READ_TEST_TEARDOWN() \
    do { \
	mock_bnode_free(); \
	unlink(test_file); \
	free(test_file); \
	test_file = NULL; \
    } while (0)

#define WRITE_TEST_SETUP(config) \
    do { \
	create_file(&test_file, ""); \
	test_expect = config; \
	bozo_isrestricted = 0; \
	memset(&bozo_nextRestartKT, 0, sizeof(bozo_nextRestartKT)); \
	memset(&bozo_nextDayKT, 0, sizeof(bozo_nextDayKT)); \
    } while (0)

#define WRITE_TEST_TEARDOWN() \
    do { \
	mock_bnode_free(); \
	unlink(test_file); \
	free(test_file); \
	test_file = NULL; \
    } while (0)

/**
 * ReadBozoFile() tests.
 */
void
test_read_bosconfig(void)
{
    char *test_name = NULL;
    int test_code;
    char *test_file = NULL;

    /*
     * ReadBozoFile() will return ok when no config file is present to support
     * a cold startup.  On a cold startup, the BosConfig (or BosInit,
     * BosConfig.new) file will not exist yet. The bosserver will start without
     * any bnodes, and a BosConfig file will be written when the first bnode is
     * created.
     */
    READ_TEST_SETUP("no file", 0, "");
    if (unlink(test_file) < 0)
	sysbail("unlink");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("sample database config", 0,
	"restrictmode 0\n"
	"restarttime 16 0 0 0 0\n"
	"checkbintime 3 0 5 0 0\n"
	"bnode simple ptserver 1\n"
	"parm /usr/afs/bin/ptserver\n"
	"end\n"
	"bnode simple vlserver 1\n"
	"parm /usr/afs/bin/vlserver\n"
	"end\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(2);
    is_int(bozo_isrestricted, 0, ".. restricted");
    is_ktime(&bozo_nextRestartKT, 16, 0, 0, 0, 0, ".. next restart time");
    is_ktime(&bozo_nextDayKT, 3, 0, 5, 0, 0, ".. next day time");
    is_bnode(0, "simple", "ptserver", 1, "/usr/afs/bin/ptserver", NULL, NULL, NULL, NULL, NULL);
    is_bnode(1, "simple", "vlserver", 1, "/usr/afs/bin/vlserver", NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("sample file server config", 0,
	"restrictmode 0\n"
	"restarttime 16 0 0 0 0\n"
	"checkbintime 3 0 5 0 0\n"
	"bnode dafs dafs 1\n"
	"parm /usr/afs/bin/dafileserver -d 1 -L\n"
	"parm /usr/afs/bin/davolserver -d 1\n"
	"parm /usr/afs/bin/salvageserver\n"
	"parm /usr/afs/bin/dasalvager\n"
	"end\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_int(bozo_isrestricted, 0, ".. restricted");
    is_ktime(&bozo_nextRestartKT, 16, 0, 0, 0, 0, ".. next restart time");
    is_ktime(&bozo_nextDayKT, 3, 0, 5, 0, 0, ".. next day time");
    is_bnode_count(1);
    is_bnode(0, "dafs", "dafs", 1,
	"/usr/afs/bin/dafileserver -d 1 -L",
	"/usr/afs/bin/davolserver -d 1",
	"/usr/afs/bin/salvageserver",
	"/usr/afs/bin/dasalvager",
	NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("empty", 0, "");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid tag", -1, "bogus\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid bool", -1, "restrictmode 2\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid integer", -1, "restrictmode foo\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid integer", 0, "restrictmode 1foo\n"); /* silently fails */
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("truncated ktime", -1, "restarttime 16\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid ktime", 0, "restarttime 16 99 0 0 0\n"); /* silently fails */
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type simple", 0,
	"bnode simple foo 1\nparm example\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(1);
    is_bnode(0, "simple", "foo", 1, "example", NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type simple missing parm", BZNOCREATE,
	"bnode simple foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type simple too many parms", BZNOCREATE,
	"bnode simple foo 1\nparm one\nparm two\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type cron", 0,
	"bnode cron foo 1\nparm one\nparm now\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(1);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type cron missing parms", BZNOCREATE,
	"bnode cron foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type cron missing time", BZNOCREATE,
	"bnode cron foo 1\nparm one\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type fs", 0,
	"bnode fs foo 1\nparm one\nparm two\nparm three\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(1);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type fs missing parms", BZNOCREATE, "bnode fs foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type dafs", 0,
	"bnode dafs foo 1\nparm one\nparm two\nparm three\nparm four\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(1);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode type dafs missing parms", BZNOCREATE,
	"bnode dafs foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(0);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid bnode type", BZBADTYPE, "bnode bogus foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("missing bnode type", -1, "bnode \nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("missing bnode instance", -1, "bnode test\n\nend");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode goal 0", 0, "bnode test foo 0\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode(0, "test", "foo", 0, NULL, NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("bnode goal 1", 0, "bnode test foo 1\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode(0, "test", "foo", 1, NULL, NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("non-zero bnode goal is accepted as 1", 0, "bnode test foo 2\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode(0, "test", "foo", 1, NULL, NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("missing bnode goal", -1, "bnode test foo\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("invalid bnode goal", -1, "bnode test foo bogus\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("missing end tag", -1, "bnode test foo 1\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("unexpected end tag", -1, "end\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("unexpected parm tag", -1, "parm foo\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("characters after end tag", 0, "bnode test foo 1\nend \n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("max parm tags", 0,
	"bnode test foo 1\n"
	"parm one\nparm two\nparm three\nparm four\nparm five\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode(0, "test", "foo", 1, "one", "two", "three", "four", "five", NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("too many parm tags", 0, /* Silent truncation. */
	"bnode test foo 1\n"
	"parm one\nparm two\nparm three\nparm four\nparm five\nparm six\nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("empty parm", 0, "bnode test foo 1\nparm \nend\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    is_bnode_count(1);
    is_bnode(0, "test", "foo", 1, "", NULL, NULL, NULL, NULL, NULL);
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("out of order tags", -1,
	"bnode dafs dafs 1\n"
	"restrictmode 0\n"
	"restarttime 16 0 0 0 0\n"
	"checkbintime 3 0 5 0 0\n"
	"parm /usr/afs/bin/dafileserver -d 1 -L\n"
	"parm /usr/afs/bin/davolserver -d 1\n"
	"parm /usr/afs/bin/salvageserver\n"
	"parm /usr/afs/bin/dasalvager\n"
	"end\n");
    is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
    READ_TEST_TEARDOWN();

    /* Notifier tests take a bit more effort since bnode_Create() verifies the
     * notifier file is present. */
    READ_TEST_SETUP("bnode notifier", 0, "");
    {
	char *notifier = NULL;
	char *config = NULL;

	create_file(&notifier, "");
	if (asprintf(&config, "bnode test foo 1 %s\nend\n", notifier) < 0)
	    sysbail("out of memory");
	write_file(test_file, config);
	is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
	is_bnode(0, "test", "foo", 1, NULL, NULL, NULL, NULL, NULL, notifier);
	unlink(notifier);
	free(notifier);
	free(config);
    }
    READ_TEST_TEARDOWN();

    READ_TEST_SETUP("max parm length", 0, "");
    {
	char *parm;
	char *config = NULL;
	/*
	 * The max parm string should be BOZO_BSSIZE (256), but sadly due to
	 * fgets() truncation in ReadBozoFile(), the largest parm string is
	 * BOZO_BSSIZE - 7 (which is 249). This is because ReadBozoFile() does
	 * not account for the "parm " at the beginning of the parm line, the
	 * newline, and the nul char (for a total deficit of 7).
	 */
	size_t max_parm_len = BOZO_BSSIZE - 7;

	parm = malloc(max_parm_len + 1);
	if (parm == NULL)
	    sysbail("out of memory");
	memset(parm, 'x', max_parm_len);
	parm[max_parm_len] = '\0';
	if (asprintf(&config, "bnode test foo 1\nparm %s\nend\n", parm) < 0)
	    sysbail("out of memory");
	write_file(test_file, config);
	is_int(ReadBozoFile(test_file), test_code, "read: %s", test_name);
	is_bnode_count(1);
	is_bnode(0, "test", "foo", 1, parm, NULL, NULL, NULL, NULL, NULL);
	free(parm);
	free(config);
    }
    READ_TEST_TEARDOWN();
}

/**
 * WriteBozoFile() tests.
 */
void
test_write_bosconfig(void)
{
    char *test_file;
    char *test_expect;

    WRITE_TEST_SETUP(
	"restrictmode 0\n"
	"restarttime 0 0 0 0 0\n"
	"checkbintime 0 0 0 0 0\n");
    is_int(WriteBozoFile(test_file), 0, "write: no bnodes");
    is_file_contents(test_file, test_expect, ".. file contents");
    WRITE_TEST_TEARDOWN();

    WRITE_TEST_SETUP(
	"restrictmode 0\n"
	"restarttime 0 0 0 0 0\n"
	"checkbintime 0 0 0 0 0\n"
	"bnode simple test 1\n"
	"parm test\n"
	"end\n");
    {
	struct bnode *b;
	if (bnode_Create("simple", "test", &b, "test",
			 NULL, NULL, NULL, NULL, NULL, 1, 0) != 0)
	    sysbail("bnode_Create");
	is_int(WriteBozoFile(test_file), 0, "write: simple bnode");
	is_file_contents(test_file, test_expect, ".. file contents");
    }
    WRITE_TEST_TEARDOWN();
}

int
main(int argc, char **argv)
{
    int code;

    if (getenv("C_TAP_VERBOSE") != NULL)
	verbose = 1;

    plan(189);
    code = bnode_Init();
    if (code)
	sysbail("bnode_Init() failed; code=%d", code);
    mock_bnode_register();

    test_read_bosconfig();
    test_write_bosconfig();

    free(last_log);
    return 0;
}
