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

/* strip the \\n from the end of the line, if it is present */
static int
StripLine(char *abuffer)
{
    char *tp;

    tp = abuffer + strlen(abuffer);	/* starts off pointing at the null  */
    if (tp == abuffer)
	return 0;		/* null string, no last character to check */
    tp--;			/* aim at last character */
    if (*tp == '\n')
	*tp = 0;
    return 0;
}

/* write one bnode's worth of entry into the file */
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

#define	MAXPARMS    20
int
ReadBozoFile(const char *aname)
{
    FILE *tfile;
    char tbuffer[BOZO_BSSIZE];
    char *tp;
    char *instp = NULL, *typep = NULL, *notifier = NULL, *notp = NULL;
    afs_int32 code;
    afs_int32 ktmask, ktday, kthour, ktmin, ktsec;
    afs_int32 i, goal;
    struct bnode *tb;
    char *parms[MAXPARMS];
    char *thisparms[MAXPARMS];
    int rmode;

    for (code = 0; code < MAXPARMS; code++)
	parms[code] = NULL;
    tfile = fopen(aname, "r");
    if (!tfile)
	return 0;		/* -1 */
    instp = malloc(BOZO_BSSIZE);
    if (!instp) {
	code = ENOMEM;
	goto fail;
    }
    typep = malloc(BOZO_BSSIZE);
    if (!typep) {
	code = ENOMEM;
	goto fail;
    }
    notp = malloc(BOZO_BSSIZE);
    if (!notp) {
	code = ENOMEM;
	goto fail;
    }
    while (1) {
	/* ok, read lines giving parms and such from the file */
	tp = fgets(tbuffer, sizeof(tbuffer), tfile);
	if (tp == (char *)0)
	    break;		/* all done */

	if (strncmp(tbuffer, "restarttime", 11) == 0) {
	    code =
		sscanf(tbuffer, "restarttime %d %d %d %d %d", &ktmask, &ktday,
		       &kthour, &ktmin, &ktsec);
	    if (code != 5) {
		code = -1;
		goto fail;
	    }
	    /* otherwise we've read in the proper ktime structure; now assign
	     * it and continue processing */
	    bozo_nextRestartKT.mask = ktmask;
	    bozo_nextRestartKT.day = ktday;
	    bozo_nextRestartKT.hour = kthour;
	    bozo_nextRestartKT.min = ktmin;
	    bozo_nextRestartKT.sec = ktsec;
	    continue;
	}

	if (strncmp(tbuffer, "checkbintime", 12) == 0) {
	    code =
		sscanf(tbuffer, "checkbintime %d %d %d %d %d", &ktmask,
		       &ktday, &kthour, &ktmin, &ktsec);
	    if (code != 5) {
		code = -1;
		goto fail;
	    }
	    /* otherwise we've read in the proper ktime structure; now assign
	     * it and continue processing */
	    bozo_nextDayKT.mask = ktmask;	/* time to restart the system */
	    bozo_nextDayKT.day = ktday;
	    bozo_nextDayKT.hour = kthour;
	    bozo_nextDayKT.min = ktmin;
	    bozo_nextDayKT.sec = ktsec;
	    continue;
	}

	if (strncmp(tbuffer, "restrictmode", 12) == 0) {
	    code = sscanf(tbuffer, "restrictmode %d", &rmode);
	    if (code != 1) {
		code = -1;
		goto fail;
	    }
	    if (rmode != 0 && rmode != 1) {
		code = -1;
		goto fail;
	    }
	    bozo_isrestricted = rmode;
	    continue;
	}

	if (strncmp("bnode", tbuffer, 5) != 0) {
	    code = -1;
	    goto fail;
	}
	notifier = notp;
	code =
	    sscanf(tbuffer, "bnode %s %s %d %s", typep, instp, &goal,
		   notifier);
	if (code < 3) {
	    code = -1;
	    goto fail;
	} else if (code == 3)
	    notifier = NULL;

	memset(thisparms, 0, sizeof(thisparms));

	for (i = 0; i < MAXPARMS; i++) {
	    /* now read the parms, until we see an "end" line */
	    tp = fgets(tbuffer, sizeof(tbuffer), tfile);
	    if (!tp) {
		code = -1;
		goto fail;
	    }
	    StripLine(tbuffer);
	    if (!strncmp(tbuffer, "end", 3))
		break;
	    if (strncmp(tbuffer, "parm ", 5)) {
		code = -1;
		goto fail;	/* no "parm " either */
	    }
	    if (!parms[i]) {	/* make sure there's space */
		parms[i] = malloc(BOZO_BSSIZE);
		if (parms[i] == NULL) {
		    code = ENOMEM;
		    goto fail;
		}
	    }
	    strcpy(parms[i], tbuffer + 5);	/* remember the parameter for later */
	    thisparms[i] = parms[i];
	}

	/* ok, we have the type and parms, now create the object */
	code =
	    bnode_Create(typep, instp, &tb, thisparms[0], thisparms[1],
			 thisparms[2], thisparms[3], thisparms[4], notifier,
			 goal ? BSTAT_NORMAL : BSTAT_SHUTDOWN, 0);
	if (code)
	    goto fail;

	/* bnode created in 'temporarily shutdown' state;
	 * check to see if we are supposed to run this guy,
	 * and if so, start the process up */
	if (goal) {
	    bnode_SetStat(tb, BSTAT_NORMAL);	/* set goal, taking effect immediately */
	} else {
	    bnode_SetStat(tb, BSTAT_SHUTDOWN);
	}
    }
    /* all done */
    code = 0;

  fail:
    if (instp)
	free(instp);
    if (typep)
	free(typep);
    if (notp)
	free(notp);
    for (i = 0; i < MAXPARMS; i++)
	if (parms[i])
	    free(parms[i]);
    if (tfile)
	fclose(tfile);
    return code;
}

/* write a new bozo file */
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
