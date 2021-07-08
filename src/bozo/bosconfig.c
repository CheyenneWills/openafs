/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 *
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include <afs/param.h>
#include <afs/stds.h>

#include <roken.h>
#include <afs/afsutil.h>

#include "bosint.h"
#include "bnode.h"
#include "bnode_internal.h"
#include "bosprototypes.h"

extern struct ktime bozo_nextRestartKT;
extern struct ktime bozo_nextDayKT;
extern int bozo_isrestricted;

struct bztemp {
    FILE *file;
};

/**
 * Bnode information read from file.
 */
struct bnode_info {
    char *type;      /**< The bnode type name */
    char *instance;  /**< The instance name */
    int fileGoal;    /**< The saved run goal */
    char *notifier;  /**< The optional notifier program */
    int nparms;      /**< The number of parms read */
    char *parm[5];   /**< Size is limited by bnode_Create() */
};

/**
 * Free a bnode_info.
 *
 * @param abi address of the bnode_info to be freed.
 */
void
free_bnode_info(struct bnode_info **abi)
{
    struct bnode_info *bi = *abi;
    int i;

    if (bi == NULL)
	return;
    free(bi->type);
    free(bi->instance);
    free(bi->notifier);
    for (i = 0; i < bi->nparms; i++)
	free(bi->parm[i]);
    free(bi);
    *abi = NULL;
}

/**
 * Parse an integer value.
 *
 * @param text  input string
 * @param rock  pointer to an integer to be set
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_int(char *text, void *rock, const char *file, int line)
{
    int code;
    int *value = rock;
    int tvalue;
    char junk; /* To catch trailing characters. */

    code = sscanf(text, "%d%c", &tvalue, &junk);
    if (code != 1) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "invalid integer value: %s\n", file, line, text);
	return BZSYNTAX;
    }
    *value = tvalue;
    return 0;
}

/**
 * Parse a boolean value.
 *
 * @note Currently, we only support '0' and '1' to represent boolean values.
 *
 * @param text  input string
 * @param rock  pointer to an integer to be set
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_bool(char *text, void *rock, const char *file, int line)
{
    int code;
    int *value = rock;
    int tvalue;

    code = parse_int(text, &tvalue, file, line);
    if (code != 0)
	return code;
    if (tvalue != 0 && tvalue != 1) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "invalid boolean value: %s\n", file, line, text);
	return BZSYNTAX;
    }
    *value = tvalue;
    return 0;
}

/**
 * Parse a ktime value.
 *
 * @param text  input string
 * @param rock  pointer to a struct ktime to be set
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_ktime(char *text, void *rock, const char *file, int line)
{
    struct ktime *value = rock;
    int code;
    int mask;
    short hour, min, sec, day;
    char junk; /* To catch trailing characters. */

    code = sscanf(text, "%d %hd %hd %hd %hd%c",
		  &mask, &day, &hour, &min, &sec, &junk);
    if (code != 5) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "unable to parse time values\n", file, line);
	return BZSYNTAX;
    }

    /* Range checks. */
     if (day < 0 || 6 < day) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "day is out of range: %hd\n", file, line, day);
	return BZSYNTAX;
    }
    if (hour < 0 || 23 < hour) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "hour is out of range: %hd\n", file, line, hour);
	return BZSYNTAX;
    }
    if (min < 0 || 59 < min) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "min is out of range: %hd\n", file, line, min);
	return BZSYNTAX;
    }
    if (sec < 0 || 59 < sec) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "sec is out of range: %hd\n", file, line, sec);
	return BZSYNTAX;
    }

    value->mask = mask;
    value->day = day;
    value->hour = hour;
    value->min = min;
    value->sec = sec;
    return 0;
}

/**
 * Process a 'bnode' tag.
 *
 * Extract the bnode type name, instance name, goal, and optional notifier
 * program. Save the parsed values in the current bnode info structure for the
 * 'end' tag.
 *
 * @param text  input string
 * @param rock  pointer to a struct bnode_info context object
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_bnode(char *text, void *rock, const char *file, int line)
{
    int code;
    struct bnode_info **value = rock;
    struct bnode_info *bi;
    char *save = NULL;
    char *type;
    char *instance;
    char *goal_token;
    char *notifier;
    int goal;

    if (*value != NULL) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "unexpected 'bnode' tag\n", file, line);
	return BZSYNTAX;
    }

    /* Extract the type, instance, goal, and notifier values. */
    type = strtok_r(text, " ", &save);
    instance = strtok_r(NULL, " ", &save);
    goal_token = strtok_r(NULL, " ", &save);
    notifier = strtok_r(NULL, "", &save); /* Optional. */

    if (type == NULL || *type == '\0' ) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "missing type\n", file, line);
	return BZSYNTAX;
    }

    if (instance == NULL || *instance == '\0') {
	bozo_Log("Syntax error in file %s, line %d; "
		 "missing instance\n", file, line);
	return BZSYNTAX;
    }

    /*
     * Valid fileGoal values are BSTAT_NORMAL (1) and BSTAT_SHUTDOWN (0).
     * However, convert any other non-zero value to BSTAT_NORMAL since
     * historically non-zero values are accepted as BSTAT_NORMAL.
     */
    if (goal_token == NULL) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "missing goal\n", file, line);
	return BZSYNTAX;
    }
    code = parse_int(goal_token, &goal, file, line);
    if (code != 0)
	return code;
    if (goal != BSTAT_NORMAL && goal != BSTAT_SHUTDOWN) {
	bozo_Log("Warning: file %s, line %d; "
		 "converting non-zero goal to 1\n", file, line);
	goal = BSTAT_NORMAL;
    }

    if (notifier != NULL && *notifier == '\0') {
	notifier = NULL;  /* Treat an empty notifier string as none. */
    }

    /* Save the bnode information for the 'end' line. */
    bi = calloc(1, sizeof(*bi));
    if (bi == NULL) {
	bozo_Log("Out of memory\n");
	return ENOMEM;
    }
    bi->type = strdup(type);
    if (bi->type == NULL)
	goto fail;
    bi->instance = strdup(instance);
    if (bi->instance == NULL)
	goto fail;
    bi->fileGoal = goal;
    if (notifier != NULL) {
	bi->notifier = strdup(notifier);
	if (bi->notifier == NULL)
	    goto fail;
    }
    bi->nparms = 0;
    *value = bi;
    return 0;

  fail:
    bozo_Log("Out of memory\n");
    free_bnode_info(&bi);
    return ENOMEM;
}

/**
 * Save the 'parm' tag contents.
 *
 * Save the parm string in the current bnode info structure for the 'end' tag.
 *
 * @param text  input string
 * @param rock  pointer to a struct bnode_info context object
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_parm(char *text, void *rock, const char *file, int line)
{
    struct bnode_info **value = rock;
    struct bnode_info *bi;
    char *parm;

    if (*value == NULL) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "unexpected 'parm' tag\n", file, line);
	return BZSYNTAX;
    }
    bi = *value;
    if (bi->nparms >= sizeof(bi->parm)/sizeof(*bi->parm)) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "maximum number of parameters exceeded\n", file, line);
	return BZSYNTAX;
    }
    parm = strdup(text);
    if (parm == NULL) {
	bozo_Log("Out of memory");
	return ENOMEM;
    }
    bi->parm[bi->nparms++] = parm;
    return 0;
}

/**
 * Process an 'end' tag.
 *
 * Create a bnode with the information gathered from the previous bnode
 * and parm tags.
 *
 * @param text  input string
 * @param rock  pointer to a struct bnode_info context object
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
parse_end(char *text, void *rock, const char *file, int line)
{
    int code;
    struct bnode_info **value = rock;
    struct bnode_info *bi;
    struct bnode *bnode;

    if (*text != '\0') {
	bozo_Log("Syntax error in file %s, line %d; "
		 "characters after 'end' tag\n", file, line);
	return BZSYNTAX;
    }
    if (*value == NULL) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "unexpected 'end' tag\n", file, line);
	return BZSYNTAX;
    }

    bi = *value;
    code = bnode_Create(bi->type, bi->instance, &bnode,
			bi->parm[0], bi->parm[1], bi->parm[2], bi->parm[3],
			bi->parm[4], bi->notifier, bi->fileGoal, 0);
    if (code != 0) {
	bozo_Log("Failed to create bnode '%s'; code=%d\n", bi->instance, code);
	return code;
    }

    /*
     * Bnode is created in the 'temporarily shutdown' state. Check to see if we
     * are supposed to run this guy, and if so, start the process up.
     */
    if (bi->fileGoal == BSTAT_NORMAL)
	bnode_SetStat(bnode, BSTAT_NORMAL); /* Takes effect immediately. */
    else
	bnode_SetStat(bnode, BSTAT_SHUTDOWN);

    /* Reset for the next bnode..end sequence. */
    free_bnode_info(value);
    return 0;
}

/**
 * Handle an invalid tag.
 *
 * @param text  input string
 * @param rock  not used
 * @param file  filename for diagnostic messages
 * @param line  line number for diagnostic messages
 * @return 0 on success
 */
static int
invalid_tag(char *text, void *rock, const char *file, int line)
{
    bozo_Log("Syntax error in file %s, line %d; "
	     "invalid tag: %s\n", file, line, text);
    return BZSYNTAX;
}

/**
 * Read the BosConfig file.
 *
 * Read and parse the BosConfig file to set the bosserver time options and the
 * list of bnodes controlled by the bosserver.
 *
 * @param[in] BosConfig file path.
 */
int
ReadBozoFile(const char *file)
{
    int code;
    FILE *fp = NULL;
    char *buffer = NULL;
    size_t bufsize = 0;
    ssize_t len;
    int line = 1;
    int state = 0;
    struct bnode_info *bnode_info = NULL;
    struct parser {
	int state;
	int next;
	char *tag;
	size_t len;
	int (*parse)(char *text, void *rock, const char *file, int line);
	void *rock;
    } *p, parsers[] = {
	{0, 0, "restrictmode ", 0, parse_bool, &bozo_isrestricted},
	{0, 0, "restarttime ", 0, parse_ktime, &bozo_nextRestartKT},
	{0, 0, "checkbintime ", 0, parse_ktime, &bozo_nextDayKT},
	{0, 1, "bnode ", 0, parse_bnode, &bnode_info},
	{1, 1, "parm ", 0, parse_parm, &bnode_info},
	{1, 0, "end", 0, parse_end, &bnode_info},
	{0, 0, "", 0, invalid_tag, NULL}, /* None of the above. */
	{1, 1, "", 0, invalid_tag, NULL}, /* None of the above. */
	{-1, -1, NULL, 0, NULL, NULL}
    };

    /* Calculate the tag lengths once. */
    for (p = parsers; p->tag; p++)
	p->len = strlen(p->tag);

    fp = fopen(file, "r");
    if (fp == NULL) {
	if (errno == ENOENT) {
	    code = 0; /* Assume a cold startup. */
	    goto done;
	}
	bozo_Log("Failed to open file %s; errno %d\n", file, errno);
	code = errno;
	goto done;
    }

    /* Parse the contents by line and save the values. */
    while ((len = getline(&buffer, &bufsize, fp)) != -1) {
	if (buffer[len - 1] == '\n')
	    buffer[len - 1] = '\0';
	for (p = parsers; p->tag; p++) {
	    if (state == p->state && strncmp(buffer, p->tag, p->len) == 0) {
		code = p->parse(buffer + p->len, p->rock, file, line);
		if (code != 0)
		    goto done;
		state = p->next;
		break;
	    }
	}
	line++;
    }
    if (bnode_info != NULL) {
	bozo_Log("Syntax error in file %s, line %d; "
		 "missing 'end' tag\n", file, line);
	code = BZSYNTAX;
	goto done;
    }
    code = 0;

  done:
    if (fp != NULL)
	fclose(fp);
    free_bnode_info(&bnode_info);
    free(buffer);
    return code;
}

/**
 * Write one bnode's worth of entry into the file.
 *
 * @param abonde  bnode to be written
 * @param arock   struct bztemp context
 * @return 0 on success
 */
static int
bzwrite(struct bnode *abnode, void *arock)
{
    struct bztemp *at = (struct bztemp *)arock;
    int i;
    afs_int32 code;

    if (abnode->notifier)
	fprintf(at->file, "bnode %s %s %d %s\n", abnode->type->name,
		abnode->name, abnode->fileGoal, abnode->notifier);
    else
	fprintf(at->file, "bnode %s %s %d\n", abnode->type->name,
		abnode->name, abnode->fileGoal);
    for (i = 0;; i++) {
	char *parm = NULL;
	code = bnode_GetParm(abnode, i, &parm);
	if (code) {
	    if (code != BZDOM)
		return code;
	    break;
	}
	fprintf(at->file, "parm %s\n", parm);
	free(parm);
    }
    fprintf(at->file, "end\n");
    return 0;
}

/**
 * Write a new bozo file.
 *
 * @param aname filename of file to be written
 * @return 0 on success
 */
int
WriteBozoFile(const char *aname)
{
    FILE *tfile;
    char *tbuffer = NULL;
    afs_int32 code;
    struct bztemp btemp;
    int ret = 0;

    if (asprintf(&tbuffer, "%s.NBZ", aname) < 0)
	return -1;

    tfile = fopen(tbuffer, "w");
    if (!tfile) {
	ret = -1;
	goto out;
    }
    btemp.file = tfile;

    fprintf(tfile, "restrictmode %d\n", bozo_isrestricted);
    fprintf(tfile, "restarttime %d %d %d %d %d\n", bozo_nextRestartKT.mask,
	    bozo_nextRestartKT.day, bozo_nextRestartKT.hour,
	    bozo_nextRestartKT.min, bozo_nextRestartKT.sec);
    fprintf(tfile, "checkbintime %d %d %d %d %d\n", bozo_nextDayKT.mask,
	    bozo_nextDayKT.day, bozo_nextDayKT.hour, bozo_nextDayKT.min,
	    bozo_nextDayKT.sec);
    code = bnode_ApplyInstance(bzwrite, &btemp);
    if (code || (code = ferror(tfile))) {	/* something went wrong */
	fclose(tfile);
	unlink(tbuffer);
	ret = code;
	goto out;
    }
    /* close the file, check for errors and snap new file into place */
    if (fclose(tfile) == EOF) {
	unlink(tbuffer);
	ret = -1;
	goto out;
    }
    code = rk_rename(tbuffer, aname);
    if (code) {
	unlink(tbuffer);
	ret = -1;
	goto out;
    }
    ret = 0;
out:
    free(tbuffer);
    return ret;
}
