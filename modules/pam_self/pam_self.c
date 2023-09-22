/*
 * Copyright (C) 2009 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms of pam_self, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 * 
 * 1. Redistributions of source code must retain any existing copyright
 * notice, and this entire permission notice in its entirety,
 * including the disclaimer of warranties.
 * 
 * 2. Redistributions in binary form must reproduce all prior and current
 * copyright notices, this list of conditions, and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * 
 * 3. The name of any author may not be used to endorse or promote
 * products derived from this software without their specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE. 
 */

#include <security/pam_modules.h>
#include <security/pam_appl.h>
#include <pwd.h>
#include <sys/syslimits.h>
#include "Logging.h"

PAM_DEFINE_LOG(self)
#define PAM_LOG PAM_LOG_self()

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	const char *user = NULL, *ruser = NULL;
	struct passwd *pwd, *rpwd;
	struct passwd pwdbuf;
	char pwbuffer[2 * PATH_MAX];
	uid_t uid, ruid;

	/* get target account */
	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS ||
	    NULL == user || 0 != getpwnam_r(user, &pwdbuf, pwbuffer, sizeof(pwbuffer), &pwd) || NULL == pwd) {
		os_log_error(PAM_LOG, "Invalid user.");
		return PAM_AUTH_ERR;
	}
	uid = pwd->pw_uid;

	/* get applicant */
	if (pam_get_item(pamh, PAM_RUSER, (const void **)&ruser) != PAM_SUCCESS ||
	    NULL == ruser || 0 != getpwnam_r(ruser, &pwdbuf, pwbuffer, sizeof(pwbuffer), &rpwd) || NULL == rpwd) {
        os_log_error(PAM_LOG, "Invalid remote user.");
		return PAM_AUTH_ERR;
	}
	ruid = rpwd->pw_uid;

	/* compare */
	if (uid != ruid) {
        os_log_error(PAM_LOG, "The provided user and remote user do not match.");
		return PAM_AUTH_ERR;
	}

	return PAM_SUCCESS;
}
