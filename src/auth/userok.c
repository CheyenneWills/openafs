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

#include <roken.h>
#include "base64.h"

#include <afs/stds.h>
#include <afs/pthread_glock.h>
#include <sys/types.h>
#ifdef AFS_NT40_ENV
#include <winsock2.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sys/file.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <stdlib.h>		/* for realpath() */
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <rx/xdr.h>
#include <rx/rx.h>
#include <rx/rx_identity.h>
#include <stdio.h>
#include <afs/afsutil.h>
#include <afs/fileutil.h>

#ifdef AFS_ATHENA_STDENV
#include <krb.h>
#endif

#include "auth.h"
#include "cellconfig.h"
#include "keys.h"
#include "afs/audit.h"

/* The display names for localauth and noauth identities; they aren't used
 * inside tickets or anything, but just serve as something to display in logs,
 * etc. */
#define AFS_LOCALAUTH_NAME "<LocalAuth>"
#define AFS_LOCALAUTH_LEN  (sizeof(AFS_LOCALAUTH_NAME)-1)
#define AFS_NOAUTH_NAME "<NoAuth>"
#define AFS_NOAUTH_LEN  (sizeof(AFS_NOAUTH_NAME)-1)

static int ParseLine(char *buffer, struct rx_identity *user);

static void
UserListFileName(struct afsconf_dir *adir,
		 char *buffer, size_t len)
{
    strcompose(buffer, len, adir->name, "/",
	       AFSDIR_ULIST_FILE, NULL);
}

#if !defined(UKERNEL)
int
afsconf_CheckAuth(void *arock, struct rx_call *acall)
{
    struct afsconf_dir *adir = (struct afsconf_dir *) arock;
    int rc;
    LOCK_GLOBAL_MUTEX;
    rc = ((afsconf_SuperUser(adir, acall, NULL) == 0) ? 10029 : 0);
    UNLOCK_GLOBAL_MUTEX;
    return rc;
}
#endif /* !defined(UKERNEL) */

static int
GetNoAuthFlag(struct afsconf_dir *adir)
{
    if (access(AFSDIR_SERVER_NOAUTH_FILEPATH, 0) == 0) {
	osi_audit(NoAuthEvent, 0, AUD_END);	/* some random server is running noauth */
	return 1;		/* if /usr/afs/local/NoAuth file exists, allow access */
    }
    return 0;
}


int
afsconf_GetNoAuthFlag(struct afsconf_dir *adir)
{
    int rc;

    LOCK_GLOBAL_MUTEX;
    rc = GetNoAuthFlag(adir);
    UNLOCK_GLOBAL_MUTEX;
    return rc;
}

void
afsconf_SetNoAuthFlag(struct afsconf_dir *adir, int aflag)
{
    afs_int32 code;

    LOCK_GLOBAL_MUTEX;
    if (aflag == 0) {
	/* turn off noauth flag */
	code = (unlink(AFSDIR_SERVER_NOAUTH_FILEPATH) ? errno : 0);
	osi_audit(NoAuthDisableEvent, code, AUD_END);
    } else {
	/* try to create file */
	code =
	    open(AFSDIR_SERVER_NOAUTH_FILEPATH, O_CREAT | O_TRUNC | O_RDWR,
		 0666);
	if (code >= 0) {
	    close(code);
	    osi_audit(NoAuthEnableEvent, 0, AUD_END);
	} else
	    osi_audit(NoAuthEnableEvent, errno, AUD_END);
    }
    UNLOCK_GLOBAL_MUTEX;
}

/*!
 * Remove an identity from the UserList file
 *
 * This function removes the given identity from the user list file.
 * For the purposes of identifying entries to remove, only the
 * type and exportedName portions of the identity are used. Callers
 * should remember that a given identity may be listed in the file in
 * a number of different ways.
 *
 * @param adir
 * 	A structure representing the configuration directory currently
 * 	in use
 * @param user
 * 	The RX identity to delete
 *
 * @returns
 * 	0 on success, an error code on failure
 */

int
afsconf_DeleteIdentity(struct afsconf_dir *adir, struct rx_identity *user)
{
    char tbuffer[1024];
    char nbuffer[1024];
    char *copy;
    FILE *tf;
    FILE *nf;
    int flag;
    char *tp;
    int found;
    struct stat tstat;
    struct rx_identity identity;
    afs_int32 code;

    LOCK_GLOBAL_MUTEX;
    UserListFileName(adir, tbuffer, sizeof tbuffer);
#ifndef AFS_NT40_ENV
    {
	/*
	 * We attempt to fully resolve this pathname, so that the rename
	 * of the temporary file will work even if UserList is a symlink
	 * into a different filesystem.
	 */
	char resolved_path[1024];

	if (realpath(tbuffer, resolved_path)) {
	    strcpy(tbuffer, resolved_path);
	}
    }
#endif /* AFS_NT40_ENV */
    tf = fopen(tbuffer, "r");
    if (!tf) {
	UNLOCK_GLOBAL_MUTEX;
	return -1;
    }
    code = stat(tbuffer, &tstat);
    if (code < 0) {
	UNLOCK_GLOBAL_MUTEX;
	return code;
    }
    strcpy(nbuffer, tbuffer);
    strcat(nbuffer, ".NXX");
    nf = fopen(nbuffer, "w+");
    if (!nf) {
	fclose(tf);
	UNLOCK_GLOBAL_MUTEX;
	return EIO;
    }
    flag = 0;
    found = 0;
    while (1) {
	/* check for our user id */
	tp = fgets(nbuffer, sizeof(nbuffer), tf);
	if (tp == NULL)
	    break;

	copy = strdup(nbuffer);
	if (copy == NULL) {
	    flag = 1;
	    break;
	}
	code = ParseLine(copy, &identity);
	if (code == 0 && rx_identity_match(user, &identity)) {
	    /* found the guy, don't copy to output file */
	    found = 1;
	} else {
	    /* otherwise copy original line to output */
	    fprintf(nf, "%s", nbuffer);
	}
	free(copy);
	rx_identity_freeContents(&identity);
    }
    fclose(tf);
    if (ferror(nf))
	flag = 1;
    if (fclose(nf) == EOF)
	flag = 1;
    strcpy(nbuffer, tbuffer);
    strcat(nbuffer, ".NXX");	/* generate new file name again */
    if (flag == 0) {
	/* try the rename */
	flag = renamefile(nbuffer, tbuffer);
	if (flag == 0)
	    flag = chmod(tbuffer, tstat.st_mode);
    } else
	unlink(nbuffer);

    /* finally, decide what to return to the caller */
    UNLOCK_GLOBAL_MUTEX;
    if (flag)
	return EIO;		/* something mysterious went wrong */
    if (!found)
	return ENOENT;		/* entry wasn't found, no changes made */
    return 0;			/* everything was fine */
}

/*!
 * Remove a legacy Kerberos 4 name from the UserList file.
 *
 * This function removes a Kerberos 4 name from the super user list. It
 * can only remove names which were added by the afsconf_AddUser interface,
 * or with an explicit Kerberos v4 type.
 *
 * @param[in] adir
 * 	A structure representing the configuration directory
 * @param[in] name
 * 	The Kerberos v4 name to remove
 *
 * @returns
 * 	0 on success, an error code upon failure.
 *
 * Note that this function is deprecated. New callers should use
 * afsconf_DeleteIdentity instead.
 */

int
afsconf_DeleteUser(struct afsconf_dir *adir, char *name)
{
    struct rx_identity *user;
    int code;

    user = rx_identity_new(RX_ID_KRB4, name, name, strlen(name));
    if (!user)
	return ENOMEM;

    code = afsconf_DeleteIdentity(adir, user);

    rx_identity_free(&user);

    return code;
}

/* This is a multi-purpose funciton for use by either
 * GetNthIdentity or GetNthUser. The parameter 'id' indicates
 * whether we are counting all identities (if true), or just
 * ones which can be represented by the old-style interfaces
 */
static int
GetNthIdentityOrUser(struct afsconf_dir *dir, int count,
		     struct rx_identity **identity, int id)
{
    bufio_p bp;
    char tbuffer[1024];
    struct rx_identity fileUser;
    afs_int32 code;

    LOCK_GLOBAL_MUTEX;
    UserListFileName(dir, tbuffer, sizeof(tbuffer));
    bp = BufioOpen(tbuffer, O_RDONLY, 0);
    if (!bp) {
	UNLOCK_GLOBAL_MUTEX;
	return EIO;
    }
    while (1) {
	code = BufioGets(bp, tbuffer, sizeof(tbuffer));
	if (code < 0)
	    break;

	code = ParseLine(tbuffer, &fileUser);
	if (code != 0)
	    break;

	if (id || fileUser.kind == RX_ID_KRB4)
	    count--;

	if (count < 0)
	    break;
        else
	    rx_identity_freeContents(&fileUser);
    }
    if (code == 0) {
	*identity = rx_identity_copy(&fileUser);
	rx_identity_freeContents(&fileUser);
    }

    BufioClose(bp);

    UNLOCK_GLOBAL_MUTEX;
    return code;
}

/*!
 * Return the Nth super user identity from the UserList
 *
 * @param[in] dir
 * 	A structure representing the configuration directory
 * @param[in] count
 * 	A count (from zero) of the entries to return from the
 * 	UserList
 * @param[out] identity
 * 	A pointer to the Nth identity
 * @returns
 * 	0 on success, non-zero on failure
 */

int
afsconf_GetNthIdentity(struct afsconf_dir *dir, int count,
		       struct rx_identity **identity)
{
   return GetNthIdentityOrUser(dir, count, identity, 1);
}

/*!
 * Return the Nth Kerberos v4 identity from the UserList
 *
 * This returns the Nth old, kerberos v4 style name from
 * the UserList file. In counting entries it skips any other
 * name types it encounters - so will hide any new-style
 * identities from its callers.
 *
 * @param[in] dir
 * 	A structure representing the configuration directory
 * @param[in] count
 * 	A count (from zero) of the entries to return from the
 * 	UserList
 * @param abuffer
 * 	A string in which to write the name of the Nth identity
 * @param abufferLen
 * 	The length of the buffer passed in abuffer
 * @returns
 * 	0 on success, non-zero on failure
 *
 * This function is deprecated, all new callers should use
 * GetNthIdentity instead. This function is particularly dangerous
 * as it will hide any new-style identities from callers.
 */

int
afsconf_GetNthUser(struct afsconf_dir *adir, afs_int32 an, char *abuffer,
		   afs_int32 abufferLen)
{
    struct rx_identity *identity;
    int code;

    code = GetNthIdentityOrUser(adir, an, &identity, 0);
    if (code == 0) {
	strlcpy(abuffer, identity->displayName, abufferLen);
	rx_identity_free(&identity);
    }
    return code;
}

/*!
 * Parse a UserList list
 *
 * Parse a line of data from a UserList file
 *
 * This parses a line of data in a UserList, and populates the passed
 * rx_identity structure with the information about the user.
 *
 * @param buffer	A string containing the line to be parsed
 * @param user		The user structure to be populated
 *
 * Note that the user->displayName, and user->exportedName.val fields
 * must be freed with free() by the caller.
 *
 * This function damages the buffer thats passed to it. Callers are
 * expected to make a copy if they want the buffer preserved.
 *
 * @return
 * 	0 on success, non-zero on failure.
 */

static int
ParseLine(char *buffer, struct rx_identity *user)
{
    char *ptr;
    char *ename;
    char *displayName;
    char *decodedName;
    char name[64+1];
    int len;
    int kind;
    int code;

    if (buffer[0] == ' ') { /* extended names have leading space */
	ptr = buffer + 1;
	code = sscanf(ptr, "%i", &kind);
	if (code != 1)
	    return EINVAL;

	strsep(&ptr, " "); /* skip the bit we just read with scanf */
	ename = strsep(&ptr, " "); /* Pull out the ename */
	displayName = strsep(&ptr, " "); /* Display name runs to the end */
	if (ename == NULL || displayName == NULL)
	    return EINVAL;

	decodedName = malloc(strlen(ename));
	if (decodedName == NULL)
	    return ENOMEM;

	len = base64_decode(ename, decodedName);
	if (len<0) {
	    free(decodedName);
	    return EINVAL;
	}

	rx_identity_populate(user, kind, displayName, decodedName, len);
	free(decodedName);

	return 0; /* Success ! */
    }

    /* No extended name, try for a legacy name */
    code = sscanf(buffer, "%64s", name);
    if (code != 1)
	return EINVAL;

    rx_identity_populate(user, RX_ID_KRB4, name, name, strlen(name));
    return 0;
}

/*!
 * Check if a given identity is in the UserList file,
 * and thus is a super user
 *
 * @param adir
 * 	A structure representing the configuration directory to check
 * @param user
 * 	The identity to check
 * @returns
 * 	True if the user is listed in the UserList, otherwise false
 */

int
afsconf_IsSuperIdentity(struct afsconf_dir *adir,
			struct rx_identity *user)
{
    bufio_p bp;
    char tbuffer[1024];
    struct rx_identity fileUser;
    int match;
    afs_int32 code;

    strcompose(tbuffer, sizeof tbuffer, adir->name, "/", AFSDIR_ULIST_FILE,
	       NULL);
    bp = BufioOpen(tbuffer, O_RDONLY, 0);
    if (!bp)
	return 0;
    match = 0;
    while (!match) {
	code = BufioGets(bp, tbuffer, sizeof(tbuffer));
        if (code < 0)
	    break;

	code = ParseLine(tbuffer, &fileUser);
	if (code != 0)
	   break;

	match = rx_identity_match(user, &fileUser);

	rx_identity_freeContents(&fileUser);
    }
    BufioClose(bp);
    return match;
}

/* add a user to the user list, checking for duplicates */
int
afsconf_AddIdentity(struct afsconf_dir *adir, struct rx_identity *user)
{
    FILE *tf;
    afs_int32 code;
    char *ename;
    char tbuffer[256];

    LOCK_GLOBAL_MUTEX;
    if (afsconf_IsSuperIdentity(adir, user)) {
	UNLOCK_GLOBAL_MUTEX;
	return EEXIST;		/* already in the list */
    }

    strcompose(tbuffer, sizeof tbuffer, adir->name, "/", AFSDIR_ULIST_FILE,
	       NULL);
    tf = fopen(tbuffer, "a+");
    if (!tf) {
	UNLOCK_GLOBAL_MUTEX;
	return EIO;
    }
    if (user->kind == RX_ID_KRB4) {
	fprintf(tf, "%s\n", user->displayName);
    } else {
	base64_encode(user->exportedName.val, user->exportedName.len,
		      &ename);
	fprintf(tf, " %d %s %s\n", user->kind, ename, user->displayName);
	free(ename);
    }
    code = 0;
    if (ferror(tf))
	code = EIO;
    if (fclose(tf))
	code = EIO;
    UNLOCK_GLOBAL_MUTEX;
    return code;
}

int
afsconf_AddUser(struct afsconf_dir *adir, char *aname)
{
    struct rx_identity *user;
    int code;

    user = rx_identity_new(RX_ID_KRB4, aname, aname, strlen(aname));
    if (user == NULL)
	return ENOMEM;

    code = afsconf_AddIdentity(adir, user);

    rx_identity_free(&user);

    return code;
}

/* special CompFindUser routine that builds up a princ and then
	calls finduser on it. If found, returns char * to user string,
	otherwise returns NULL. The resulting string should be immediately
	copied to other storage prior to release of mutex. */
static int
CompFindUser(struct afsconf_dir *adir, char *name, char *sep, char *inst,
	     char *realm, struct rx_identity **identity)
{
    static char fullname[MAXKTCNAMELEN + MAXKTCNAMELEN + MAXKTCREALMLEN + 3];
    struct rx_identity *testId;

    /* always must have name */
    if (!name || !name[0]) {
	return 0;
    }
    strcpy(fullname, name);

    /* might have instance */
    if (inst && inst[0]) {
	if (!sep || !sep[0]) {
	    return 0;
	}

	strcat(fullname, sep);
	strcat(fullname, inst);
    }

    /* might have realm */
    if (realm && realm[0]) {
	strcat(fullname, "@");
	strcat(fullname, realm);
    }

    testId = rx_identity_new(RX_ID_KRB4, fullname, fullname, strlen(fullname));
    if (afsconf_IsSuperIdentity(adir, testId)) {
	if (identity)
	     *identity = testId;
	else
	     rx_identity_free(&testId);
	return 1;
    }

    rx_identity_free(&testId);
    return 0;
}

static int
kerberosSuperUser(struct afsconf_dir *adir, char *tname, char *tinst,
		  char *tcell, struct rx_identity **identity)
{
    char tcell_l[MAXKTCREALMLEN] = "";
    char *tmp;
    static char lcell[MAXCELLCHARS] = "";
    static char lrealms[AFS_NUM_LREALMS][AFS_REALM_SZ];
    static int  num_lrealms = -1;
    int lrealm_match = 0, i;
    int flag;

    /* generate lowercased version of cell name */
    if (tcell) {
	strcpy(tcell_l, tcell);
	tmp = tcell_l;
	while (*tmp) {
	    *tmp = tolower(*tmp);
	    tmp++;
	}
    }

    /* determine local cell name. It's static, so will only get
     * calculated the first time through */
    if (!lcell[0])
	afsconf_GetLocalCell(adir, lcell, sizeof(lcell));

    /* if running a krb environment, also get the local realm */
    /* note - this assumes AFS_REALM_SZ <= MAXCELLCHARS */
    /* just set it to lcell if it fails */
    if (num_lrealms == -1) {
	for (i=0; i<AFS_NUM_LREALMS; i++) {
	    if (afs_krb_get_lrealm(lrealms[i], i) != 0 /*KSUCCESS*/)
		break;
	}

	if (i == 0) {
	    strncpy(lrealms[0], lcell, AFS_REALM_SZ);
	    num_lrealms = 1;
	} else {
	    num_lrealms = i;
	}
    }

    /* See if the ticket cell matches one of the local realms */
    lrealm_match = 0;
    for ( i=0;i<num_lrealms;i++ ) {
	if (!strcasecmp(lrealms[i], tcell)) {
	    lrealm_match = 1;
	    break;
	}
    }

    /* If yes, then make sure that the name is not present in
     * an exclusion list */
    if (lrealm_match) {
	char uname[MAXKTCNAMELEN + MAXKTCNAMELEN + MAXKTCREALMLEN + 3];
	if (tinst && tinst[0])
	    snprintf(uname,sizeof(uname),"%s.%s@%s",tname,tinst,tcell);
	else
	    snprintf(uname,sizeof(uname),"%s@%s",tname,tcell);

	if (afs_krb_exclusion(uname))
	    lrealm_match = 0;
    }

    /* start with no authorization */
    flag = 0;

    /* localauth special case */
    if ((tinst == NULL || strlen(tinst) == 0) &&
	(tcell == NULL || strlen(tcell) == 0)
	&& !strcmp(tname, AUTH_SUPERUSER)) {
	if (identity)
	    *identity = rx_identity_new(RX_ID_KRB4, AFS_LOCALAUTH_NAME,
	                                AFS_LOCALAUTH_NAME, AFS_LOCALAUTH_LEN);
	flag = 1;

	/* cell of connection matches local cell or one of the realms */
    } else if (!strcasecmp(tcell, lcell) || lrealm_match) {
	if (CompFindUser(adir, tname, ".", tinst, NULL, identity)) {
	    flag = 1;
	}
	/* cell of conn doesn't match local cell or realm */
    } else {
	if (CompFindUser(adir, tname, ".", tinst, tcell, identity)) {
	    flag = 1;
	} else if (CompFindUser(adir, tname, ".", tinst, tcell_l, identity)) {
	    flag = 1;
	}
    }

    return flag;
}

static int
rxkadSuperUser(struct afsconf_dir *adir, struct rx_call *acall,
	       struct rx_identity **identity)
{
    char tname[MAXKTCNAMELEN];	/* authentication from ticket */
    char tinst[MAXKTCNAMELEN];
    char tcell[MAXKTCREALMLEN];

    afs_uint32 exp;
    int code;

    /* get auth details from server connection */
    code = rxkad_GetServerInfo(acall->conn, NULL, &exp, tname, tinst, tcell,
			       NULL);
    if (code)
	return 0;		/* bogus connection/other error */

    return kerberosSuperUser(adir, tname, tinst, tcell, identity);
}

/*!
 * Check whether the user authenticated on a given RX call is a super
 * user or not. If they are, return a pointer to the identity of that
 * user.
 *
 * @param[in] adir
 * 	The configuration directory currently in use
 * @param[in] acall
 * 	The RX call whose authenticated identity is being checked
 * @param[out] identity
 * 	The RX identity of the user. Caller must free this structure.
 * @returns
 * 	True if the user is a super user, or if the server is running
 * 	in noauth mode. Otherwise, false.
 */
afs_int32
afsconf_SuperIdentity(struct afsconf_dir *adir, struct rx_call *acall,
		      struct rx_identity **identity)
{
    struct rx_connection *tconn;
    afs_int32 code;
    int flag;

    LOCK_GLOBAL_MUTEX;
    if (!adir) {
	UNLOCK_GLOBAL_MUTEX;
	return 0;
    }

    if (afsconf_GetNoAuthFlag(adir)) {
	if (identity)
	    *identity = rx_identity_new(RX_ID_KRB4, AFS_NOAUTH_NAME,
	                                AFS_NOAUTH_NAME, AFS_NOAUTH_LEN);
	UNLOCK_GLOBAL_MUTEX;
	return 1;
    }

    tconn = rx_ConnectionOf(acall);
    code = rx_SecurityClassOf(tconn);
    if (code == 0) {
	UNLOCK_GLOBAL_MUTEX;
	return 0;		/* not authenticated at all, answer is no */
    } else if (code == 1) {
	/* bcrypt tokens */
	UNLOCK_GLOBAL_MUTEX;
	return 0;		/* not supported any longer */
    } else if (code == 2) {
	flag = rxkadSuperUser(adir, acall, identity);
	UNLOCK_GLOBAL_MUTEX;
	return flag;
    } else {			/* some other auth type */
	UNLOCK_GLOBAL_MUTEX;
	return 0;		/* mysterious, just say no */
    }
}

/*!
 * Check whether the user authenticated on a given RX call is a super
 * user or not. If they are, return a pointer to the name of that
 * user.
 *
 * @param[in] adir
 * 	The configuration directory currently in use
 * @param[in] acall
 * 	The RX call whose authenticated identity is being checked
 * @param[out] namep
 * 	A printable version of the name of the user
 * @returns
 * 	True if the user is a super user, or if the server is running
 * 	in noauth mode. Otherwise, false.
 *
 * This function is provided for backwards compatibility. New callers
 * should use the afsconf_SuperIdentity function.
 */

afs_int32
afsconf_SuperUser(struct afsconf_dir *adir, struct rx_call *acall,
		  char *namep)
{
    struct rx_identity *identity;
    int ret;

    if (namep) {
	ret = afsconf_SuperIdentity(adir, acall, &identity);
	if (ret) {
	    if (identity->kind == RX_ID_KRB4) {
		strlcpy(namep, identity->displayName, MAXKTCNAMELEN-1);
	    } else {
		snprintf(namep, MAXKTCNAMELEN-1, "eName: %s",
			 identity->displayName);
	    }
	    rx_identity_free(&identity);
	}
    } else {
	ret = afsconf_SuperIdentity(adir, acall, NULL);
    }

    return ret;
}
