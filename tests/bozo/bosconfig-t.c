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

#include <afs/afsutil.h>
#include <afs/bosint.h>
#include <afs/bnode.h>
#include <tests/tap/basic.h>

#include <bnode_internal.h> /* Required for bosprotoypes.h ?! */
#include <bosprototypes.h>

#include "common.h"

/* Globals serialized to the BosConfig file. */
int bozo_isrestricted;
struct ktime bozo_nextRestartKT;
struct ktime bozo_nextDayKT;

/* Stubs to satisfy bosconfig/bnode dependencies. */
int bozo_restdisable = 0;
const char *bozo_fileName = NULL;
char *DoCore = NULL;
void bozo_Log(const char *format, ...) {}
void * bozo_ShutdownAndExit(void *param) { return NULL; }
int osi_audit(void ) { return 0; }

/*
 * Mock bnode for testing ReadBozoFile().
 */

/*
 * The mock operation prototypes for mock_ops, must be declared before the
 * mock_create() function definition, since mock_ops is referenced in mock_create().
 * (Unfortunately, the bnode ops argument passed to bnode_Register() is not used
 * to create new bnode instances at this time.)
 */
struct bnode *mock_create(char *a0, char *a1, char *a2, char *a3, char *a4, char *a5);
int mock_timeout(struct bnode *bnode);
int mock_hascore(struct bnode *bnode);
int mock_restartp(struct bnode *bnode);
int mock_getstat(struct bnode *bnode, afs_int32 *status);
int mock_setstat(struct bnode *bnode, afs_int32 status);
int mock_delete(struct bnode *bnode);
int mock_procexit(struct bnode *bnode, struct bnode_proc *proc);
int mock_getstring(struct bnode *bnode, char *abuffer, afs_int32 alen);
int mock_getparm(struct bnode *bnode, afs_int32 a0, char *a1, afs_int32 a2);
int mock_procstarted(struct bnode *bnode, struct bnode_proc *proc);

struct bnode_ops mock_ops = {
    mock_create,
    mock_timeout,
    mock_getstat,
    mock_setstat,
    mock_delete,
    mock_procexit,
    mock_getstring,
    mock_getparm,
    mock_restartp,
    mock_hascore,
    mock_procstarted
};

/*
 * Our mock bnode just saves the arguments parsed from the BosConfig file.
 */
struct mock_bnode {
    struct bnode b;
    int status;
    char *name;
    char *args[5];
};

/**
 * Helper function to copy bnode name and arg strings.
 *
 * Pass NULL pointer if given, otherwise copy the given string.
 */
static char *
copy_arg(char *s)
{
    char *arg;

    if (s == NULL)
	arg = NULL;
    else {
	arg = strdup(s);
	if (arg == NULL)
	    sysbail("strdup");
    }
    return arg;
}

/**
 * Create a mock bnode for testing.
 *
 * This function is called indirectly by ReadBozoFile() when
 * the bnode data has been read from the BosConfig file.
 */
struct bnode *
mock_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4)
{
    int code;
    struct mock_bnode *mock;

    mock = bcalloc(1, sizeof(*mock)); /* Bails on failure. */
    mock->name = copy_arg(name);
    mock->args[0] = copy_arg(a0);
    mock->args[1] = copy_arg(a1);
    mock->args[2] = copy_arg(a2);
    mock->args[3] = copy_arg(a3);
    mock->args[4] = copy_arg(a4);

    code = bnode_InitBnode((struct bnode *)mock, &mock_ops, name);
    if (code != 0)
	sysbail("bnode_InitBnode() failed; code=%d", code);

    return (struct bnode *)mock;
}

/**
 * Set the mock bnode run goal.
 *
 * This function is called indirectly during ReadBozoFile() after
 * the bnode is created by ReadBozoFile().
 */
int
mock_setstat(struct bnode *bnode, afs_int32 status)
{
    struct mock_bnode *mock = (struct mock_bnode *)bnode;
    mock->status = status;
    return 0;
}

/**
 * Delete a mock bnode.
 */
int
mock_delete(struct bnode *bnode)
{
    struct mock_bnode *mock = (struct mock_bnode *)bnode;
    int i;

    free(mock->name);
    for (i = 0; i < sizeof(mock->args)/sizeof(*mock->args); i++)
	free(mock->args[i]);
    free(mock);
    return 0;
}

/* Noop stubs. */
int mock_hascore(struct bnode *bnode) { return 0; }
int mock_restartp(struct bnode *bnode) { return 0; }
int mock_timeout(struct bnode *bnode) { return 0; }
int mock_getstat(struct bnode *bnode, afs_int32 *status) { return 0; }
int mock_procexit(struct bnode *bnode, struct bnode_proc *proc) { return 0; }
int mock_getstring(struct bnode *bnode, char *abuffer, afs_int32 alen) { return 0; }
int mock_getparm(struct bnode *bnode, afs_int32 a0, char *a1, afs_int32 a2) { return 0; }
int mock_procstarted(struct bnode *bnode, struct bnode_proc *proc) { return 0; }

/*
 * Dump the mock bnodes (for debugging).
 */
int
mock_dump(struct bnode *bnode, void *rock)
{
    int i;
    struct mock_bnode *mock = (struct mock_bnode *)bnode;
    char *type = mock->b.type->name;
    char *name = mock->name;

    diag("bnode:");
    diag("  type: '%s'", (type != NULL ? type : "(nil)"));
    diag("  name: '%s'", (name != NULL ? name : "(nil)"));
    diag("  args:");
    for (i = 0; i < sizeof(mock->args)/sizeof(*mock->args); i++) {
        char *arg = mock->args[i];
	diag("    - '%s'", (arg != NULL ? arg : "(nil)"));
    }
    return 0;
}

void
dump_bnodes(void)
{
    bnode_ApplyInstance(mock_dump, NULL);
}

/*
 * Count the number of bnodes.
 */
int
mock_count(struct bnode *bnode, void *rock)
{
    int *count = rock;
    (*count)++;
    return 0;
}

int
count_bnodes(void)
{
    int count = 0;
    bnode_ApplyInstance(mock_count, &count);
    return count;
}

/*
 * Find a bnode by index.
 */
struct mock_cursor {
    int index;
    int count;
    struct mock_bnode *results;
};

int
mock_find_by_index(struct bnode *b, void *rock)
{
    struct mock_cursor *cursor = rock;
    if (cursor->index == cursor->count) {
	cursor->results = (struct mock_bnode*)b;
	return 1;
    }
    cursor->count++;
    return 0;
}

struct mock_bnode*
find_bnode(int index)
{
    struct mock_cursor cursor = {index, 0, NULL};

    bnode_ApplyInstance(mock_find_by_index, &cursor);
    return cursor.results;
}

/*
 * Delete all of the mock bnodes.
 *
 * Note: The mock_zap() function is needed because bnode_Delete() calls
 * WriteBozoFile() to update the bosserver's BosConfig file whenever a bnode is
 * removed, and we want to avoid that implied call the WriteBozoFile() in this
 * test program.
 *
 * bnode_ApplyInstance() supports removing queue elements while iterating
 * over the bnodes, so we can just use that function and zap each bnode.
 */
int
mock_zap(struct bnode *bnode, void *rock)
{
    opr_queue_Remove(&bnode->q);
    free(bnode->name);
    mock_delete(bnode);
    return 0;
}

void
zap_bnodes(void)
{
    bnode_ApplyInstance(mock_zap, NULL);
    if (count_bnodes() != 0)
	sysbail("zap_bnodes");
}

/**
 * Create a config file in the temp directory.
 *
 * Allocates and returns the name of the created file to the caller, which must
 * be freed after use.
 */
char *
create_file(char *tmpdir, const char *contents)
{
    char *filename = afstest_asprintf("%s/BosConfig", tmpdir);
    int code;
    FILE *fp;

    fp = fopen(filename, "w");
    if (fp == NULL)
	sysbail("failed to open test file %s", filename);
    code = fprintf(fp, "%s", contents);
    if (code < 0)
	sysbail("failed to write test file %s; code=%d", filename, code);
    code = fclose(fp);
    if (code < 0)
	sysbail("failed close test file %s; code=%d", filename, code);
    return filename;
}

/* Test ktime values. */
void
is_ktime(struct ktime *t, int mask, short hour, short min,
         short sec, short day, const char *msg)
{
    is_int(t->mask, mask, "%s: mask", msg);
    is_int(t->hour, hour, "%s: hour", msg);
    is_int(t->min, min, "%s: min", msg);
    is_int(t->sec, sec, "%s: sec", msg);
    is_int(t->day, day, "%s: day", msg);
}

/* Test mock bnode values. Looks up the bnode by index. */
void
is_bnode(int index, char *type, char *name, int status,
         char *a0, char *a1, char *a2, char *a3, char *a4)
{
    struct mock_bnode *b;

    b = find_bnode(index);
    ok(b != NULL, "bnode %d: found", index);
    if (b == NULL) {
	skip_block(7, "bnode checks; bnode %d not found", index);
    } else {
	is_string(b->b.type->name, type, "bnode %d: type", index);
	is_string(b->name, name, "bnode %d: name", index);
	is_int(b->status, status, "bnode %d: status", index);
	is_string(b->args[0], a0, "bnode %d: arg 0", index);
	is_string(b->args[1], a1, "bnode %d: arg 1", index);
	is_string(b->args[2], a2, "bnode %d: arg 2", index);
	is_string(b->args[3], a3, "bnode %d: arg 3", index);
	is_string(b->args[4], a4, "bnode %d: arg 4", index);
    }
}

#define TEST_SETUP() \
    do { \
	diag("test setup"); \
	bozo_isrestricted = -1; \
	memset(&bozo_nextRestartKT, -1, sizeof(bozo_nextRestartKT)); \
	memset(&bozo_nextDayKT, -1, sizeof(bozo_nextDayKT)); \
	filename = create_file(tmpdir, config); \
    } while (0)

#define TEST_TEARDOWN() \
    do { \
	diag("test teardown"); \
	zap_bnodes(); \
	unlink(filename); \
	free(filename); \
    } while (0)

void
test_read_a(char *tmpdir)
{
    int code;
    char *filename;
    char *config =
	"restrictmode 0\n"
	"restarttime 16 0 0 0 0\n"
	"checkbintime 3 0 5 0 0\n"
	"bnode simple ptserver 1\n"
	"parm /usr/afs/bin/ptserver\n"
	"end\n";

    TEST_SETUP();
    code = ReadBozoFile(filename);
    is_int(code, 0, "ReadBozoFile: example bnodes");
    is_int(count_bnodes(), 1, "number of created bnodes");
    is_int(bozo_isrestricted, 0, "bozo_isrestricted");
    is_ktime(&bozo_nextRestartKT, 16, 0, 0, 0, 0, "next restrart time");
    is_ktime(&bozo_nextDayKT, 3, 5, 0, 0, 0, "next day time");
    is_bnode(0, "simple", "ptserver", 1, "/usr/afs/bin/ptserver", NULL, NULL, NULL, NULL);
    TEST_TEARDOWN();
}

void
test_read_b(char *tmpdir)
{
    int code;
    char *filename;
    char *config =
	"restrictmode 0\n"
	"restarttime 16 0 0 0 0\n"
	"checkbintime 3 0 5 0 0\n"
	"bnode simple ptserver 1\n"
	"parm /usr/afs/bin/ptserver\n"
	"end\n"
	"bnode simple vlserver 1\n"
	"parm /usr/afs/bin/vlserver\n"
	"end\n"
	"bnode dafs dafs 1\n"
	"parm /usr/afs/bin/dafileserver -d 1 -L\n"
	"parm /usr/afs/bin/davolserver -d 1\n"
	"parm /usr/afs/bin/salvageserver\n"
	"parm /usr/afs/bin/dasalvager\n"
	"end\n";

    TEST_SETUP();
    code = ReadBozoFile(filename);
    is_int(code, 0, "ReadBozoFile: example bnodes");
    is_int(count_bnodes(), 3, "number of created bnodes");
    is_int(bozo_isrestricted, 0, "bozo_isrestricted");
    is_ktime(&bozo_nextRestartKT, 16, 0, 0, 0, 0, "next restrart time");
    is_ktime(&bozo_nextDayKT, 3, 5, 0, 0, 0, "next day time");
    is_bnode(0, "simple", "ptserver", 1, "/usr/afs/bin/ptserver", NULL, NULL, NULL, NULL);
    is_bnode(1, "simple", "vlserver", 1, "/usr/afs/bin/vlserver", NULL, NULL, NULL, NULL);
    is_bnode(2, "dafs", "dafs", 1,
        "/usr/afs/bin/dafileserver -d 1 -L",
        "/usr/afs/bin/davolserver -d 1",
        "/usr/afs/bin/salvageserver",
        "/usr/afs/bin/dasalvager",
        NULL);
    TEST_TEARDOWN();
}

int
main(int argc, char **argv)
{
    int code;
    char *tmpdir;

    /* Initialization */
    plan_lazy();

    tmpdir = afstest_asprintf("%s/afs_test_bosconfig_XXXXXX", gettmpdir());
    afstest_mkdtemp(tmpdir);

    code = bnode_Init();
    if (code)
	sysbail("bnode_Init() failed; code=%d", code);
    bnode_Register("simple", &mock_ops, 1);
    bnode_Register("cron", &mock_ops, 2);
    bnode_Register("fs", &mock_ops, 3);
    bnode_Register("dafs", &mock_ops, 4);

    code = ReadBozoFile("no-such-file");
    /* Note: ReadBozoFile() silently fails when the file cannot be opened! */
    is_int(code, 0, "read: file not found");

    test_read_a(tmpdir);
    test_read_b(tmpdir);

    rmdir(tmpdir);
    free(tmpdir);
    return 0;
}
