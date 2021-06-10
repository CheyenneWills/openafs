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

/*
 * Mock bnode for testing ReadBozoFile().
 */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>
#include <afs/afsutil.h>
#include <afs/bosint.h>
#include <afs/bnode.h>
#include <tests/tap/basic.h>

#include <bnode_internal.h> /* Required for bosprotoypes.h ?! */
#include <bosprototypes.h>

#include "mock-bnode.h"

/* Forward declarations for mock_ops. */
static struct bnode *mock_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4);
static int mock_setstat(struct bnode *bnode, afs_int32 status);
static int mock_getparm(struct bnode *bnode, afs_int32 a0, char **a1);
static int mock_delete(struct bnode *bnode);

/* Noop stubs. */
static int mock_timeout(struct bnode *bnode) { return 0; }
static int mock_getstat(struct bnode *bnode, afs_int32 *status) { return 0; }
static int mock_procexit(struct bnode *bnode, struct bnode_proc *proc) { return 0; }
static int mock_getstring(struct bnode *bnode, char **abuffer) { return 0; }
static int mock_restartp(struct bnode *bnode) { return 0; }
static int mock_hascore(struct bnode *bnode) { return 0; }
static int mock_procstarted(struct bnode *bnode, struct bnode_proc *proc) { return 0; }

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


struct mock_cursor {
    int index;
    int count;
    struct mock_bnode *results;
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
static struct bnode *
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
 * Check for expected simple bnode arguments.
 */
static struct bnode *
mock_simple_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4)
{
    if (name == NULL || a0 == NULL)
	return NULL;
    if (a1 != NULL || a2 != NULL || a3 != NULL || a4 != NULL)
	return NULL;
    return mock_create(name, a0, a1, a2, a3, a4);
}

/**
 * Check for expected cron bnode arguments.
 */
static struct bnode *
mock_cron_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4)
{
    if (name == NULL || a0 == NULL || a1 == NULL)
	return NULL;
    if (a2 != NULL || a3 != NULL || a4 != NULL)
	return NULL;
    return mock_create(name, a0, a1, a2, a3, a4);
}

/**
 * Check for expected fs bnode arguments.
 */
static struct bnode *
mock_fs_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4)
{
    if (name == NULL || a0 == NULL || a1 == NULL || a2 == NULL)
	return NULL;
    if (a3 != NULL || a4 != NULL)
	return NULL;
    return mock_create(name, a0, a1, a2, a3, a4);
}

/**
 * Check for expected fs bnode arguments.
 */
static struct bnode *
mock_dafs_create(char *name, char *a0, char *a1, char *a2, char *a3, char *a4)
{
    if (name == NULL || a0 == NULL || a1 == NULL || a2 == NULL || a3 == NULL)
	return NULL;
    if (a4 != NULL)
	return NULL;
    return mock_create(name, a0, a1, a2, a3, a4);
}

/**
 * Set the mock bnode status.
 *
 * This function is called indirectly during ReadBozoFile() after
 * the bnode is created by ReadBozoFile().
 */
static int
mock_setstat(struct bnode *bnode, afs_int32 status)
{
    struct mock_bnode *mock = (struct mock_bnode *)bnode;
    mock->status = status;
    return 0;
}

/**
 * Get a mock bnode parm string.
 *
 * This stub is called by WriteBozoFile() in the write tests. The
 * returned string is freed by WriteBozoFile().
 */
static int
mock_getparm(struct bnode *bnode, afs_int32 index, char **buffer)
{
    struct mock_bnode *mock = (struct mock_bnode *)bnode;

    *buffer = NULL;
    if (index < 0 || index >= (sizeof(mock->args)/sizeof(*mock->args)))
	return BZDOM;
    if (mock->args[index] == NULL)
	return BZDOM;
    *buffer = strdup(mock->args[index]);
    if (*buffer == NULL)
	sysbail("strdup");
    return 0;
}

/**
 * Delete a mock bnode.
 */
static int
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

static void
diag_string(char *label, char *s)
{
    if (s == NULL)
	diag("%s: (null)", label);
    else
	diag("%s: '%s' (%ld)", label, s, strlen(s));
}

/**
 * Dump a mock bnode to stderr (for debugging).
 */
static int
mock_dump(struct bnode *bnode, void *rock)
{
    int i;
    struct mock_bnode *mock = (struct mock_bnode *)bnode;

    diag("bnode:");
    diag("  status: %d", mock->status);
    diag_string("  type:", mock->b.type->name);
    diag_string("  name:", mock->name);
    diag("  args:");
    for (i = 0; i < sizeof(mock->args)/sizeof(*mock->args); i++) {
	char *arg = mock->args[i];
	diag_string("    - ", arg);
    }
    diag_string("  notifier:", mock->b.notifier);
    return 0;
}

/**
 * Iterator to count bnodes.
 */
static int
mock_count(struct bnode *bnode, void *rock)
{
    int *count = rock;
    (*count)++;
    return 0;
}

/**
 * Iterator to find a bnode by index.
 */
static int
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

/**
 * Iterator to delete mock bnodes.
 *
 * Note: mock_zap() is used to free the mock bnode instead of bnode_Delete(),
 * since bnode_Delete() attempts to update the bosservers's BosConfig file when
 * the bnode is deleted, and we want to avoid that in this test program.
 */
static int
mock_zap(struct bnode *bnode, void *rock)
{
    opr_queue_Remove(&bnode->q);
    free(bnode->name);
    mock_delete(bnode);
    return 0;
}

/**
 * Register mock bnodes for the usual types plus a special test bnode type.
 */
struct bnode_ops mock_simple_ops;
struct bnode_ops mock_cron_ops;
struct bnode_ops mock_fs_ops;
struct bnode_ops mock_dafs_ops;
void
mock_bnode_register(void)
{
    memcpy(&mock_simple_ops, &mock_ops, sizeof(mock_simple_ops));
    memcpy(&mock_cron_ops, &mock_ops, sizeof(mock_cron_ops));
    memcpy(&mock_fs_ops, &mock_ops, sizeof(mock_fs_ops));
    memcpy(&mock_dafs_ops, &mock_ops, sizeof(mock_simple_ops));

    mock_simple_ops.create = mock_simple_create;
    mock_cron_ops.create = mock_cron_create;
    mock_fs_ops.create = mock_fs_create;
    mock_dafs_ops.create = mock_dafs_create;

    bnode_Register("simple", &mock_simple_ops, 1);
    bnode_Register("cron", &mock_cron_ops, 2);
    bnode_Register("fs", &mock_fs_ops, 3);
    bnode_Register("dafs", &mock_dafs_ops, 4);

    bnode_Register("test", &mock_ops, 0);
}

/**
 * Dump the mock bnodes (for debugging).
 */
void
mock_bnode_dump(void)
{
    bnode_ApplyInstance(mock_dump, NULL);
}

/**
 * Return the number of bnodes.
 */
int
mock_bnode_count(void)
{
    int count = 0;
    bnode_ApplyInstance(mock_count, &count);
    return count;
}

/**
 * Find a bnode by index.
 *
 * Returns NULL when not found.
 */
struct mock_bnode*
mock_bnode_find(int index)
{
    struct mock_cursor cursor = {index, 0, NULL};

    bnode_ApplyInstance(mock_find_by_index, &cursor);
    return cursor.results;
}

/**
 * Delete the mock bnodes.
 *
 * bnode_ApplyInstance() supports removing queue elements while iterating
 * over the bnodes, so we can just use that function to zap each bnode.
 */
void
mock_bnode_free(void)
{
    bnode_ApplyInstance(mock_zap, NULL);
    if (mock_bnode_count() != 0)
	sysbail("mock_bnode_free");
}
