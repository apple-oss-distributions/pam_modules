#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>


#include <CoreFoundation/CoreFoundation.h>
#include <DirectoryService/DirectoryService.h>
#include <OpenDirectory/OpenDirectory.h>
#include <OpenDirectory/OpenDirectoryPriv.h>
#include <ServerInformation/ServerInformation.h>

#include "Common.h"
#include "Logging.h"

#ifdef PAM_USE_OS_LOG
PAM_DEFINE_LOG(Common)
#define PAM_LOG PAM_LOG_Common()
#endif

enum {
	kWaitSeconds       =  1,
	kMaxIterationCount = 30
};

int
cstring_to_cfstring(const char *val, CFStringRef *buffer)
{
	int retval = PAM_BUF_ERR;

	if (NULL == val || NULL == buffer) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	*buffer = CFStringCreateWithCString(kCFAllocatorDefault, val, kCFStringEncodingUTF8);
	if (NULL == *buffer) {
		_LOG_DEBUG("CFStringCreateWithCString() failed");
		retval = PAM_BUF_ERR;
		goto cleanup;
	}

	retval =  PAM_SUCCESS;

cleanup:
	if (PAM_SUCCESS != retval)
		_LOG_ERROR("failed: %d", retval);

	return retval;
}

int
cfstring_to_cstring(const CFStringRef val, char **buffer)
{
	CFIndex maxlen = 0;
	int retval = PAM_BUF_ERR;

	if (NULL == val || NULL == buffer) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	maxlen = CFStringGetMaximumSizeForEncoding(CFStringGetLength(val), kCFStringEncodingUTF8);
	*buffer = calloc(maxlen + 1, sizeof(char));
	if (NULL == *buffer) {
		_LOG_DEBUG("malloc() failed");
		retval = PAM_BUF_ERR;
		goto cleanup;
	}

	if (CFStringGetCString(val, *buffer, maxlen + 1, kCFStringEncodingUTF8)) {
		retval =  PAM_SUCCESS;
	} else {
		_LOG_DEBUG("CFStringGetCString failed.");
		free(*buffer);
		*buffer = NULL;
	}

cleanup:
	if (PAM_SUCCESS != retval)
		_LOG_ERROR("failed: %d", retval);

	return retval;
}

int
od_record_create(pam_handle_t *pamh, ODRecordRef *record, CFStringRef cfUser)
{
	int retval = PAM_SERVICE_ERR;
	const int attr_num = 5;

	ODNodeRef cfNode = NULL;
	CFErrorRef cferror = NULL;
	CFArrayRef attrs = NULL;
	CFTypeRef cfVals[attr_num];

	if (NULL == record || NULL == cfUser) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	int current_iterations = 0;

	cfNode = ODNodeCreateWithNodeType(kCFAllocatorDefault,
					  kODSessionDefault,
					  eDSAuthenticationSearchNodeName,
					  &cferror);
	if (NULL == cfNode || NULL != cferror) {
		_LOG_ERROR("ODNodeCreateWithNodeType failed.");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	cfVals[0] = kODAttributeTypeAuthenticationAuthority;
	cfVals[1] = kODAttributeTypeHomeDirectory;
	cfVals[2] = kODAttributeTypeNFSHomeDirectory;
	cfVals[3] = kODAttributeTypeUserShell;
	cfVals[4] = kODAttributeTypeUniqueID;
	attrs = CFArrayCreate(kCFAllocatorDefault, cfVals, (CFIndex)attr_num, &kCFTypeArrayCallBacks);
	if (NULL == attrs) {
		_LOG_DEBUG("CFArrayCreate() failed");
		retval = PAM_BUF_ERR;
		goto cleanup;
	}

	while (current_iterations <= kMaxIterationCount) {
		CFIndex unreachable_count = 0;
		CFArrayRef unreachable_nodes = ODNodeCopyUnreachableSubnodeNames(cfNode, NULL);
		if (unreachable_nodes) {
			unreachable_count = CFArrayGetCount(unreachable_nodes);
			CFRelease(unreachable_nodes);
			_LOG_DEBUG("%lu OD nodes unreachable.", unreachable_count);
		}

		*record = ODNodeCopyRecord(cfNode, kODRecordTypeUsers, cfUser, attrs, &cferror);
		if (*record)
			break;
		if (0 == unreachable_count)
			break;

		_LOG_DEBUG("Waiting %d seconds for nodes to become reachable", kWaitSeconds);
		sleep(kWaitSeconds);
		++current_iterations;
	}

	if (*record) {
		retval = PAM_SUCCESS;
	} else {
		retval = PAM_USER_UNKNOWN;
	}

cleanup:
	CFReleaseSafe(attrs);
	CFReleaseSafe(cferror);
	CFReleaseSafe(cfNode);

	if (PAM_SUCCESS != retval) {
		_LOG_ERROR("failed: %d", retval);
		if (record != NULL)
			CFReleaseNull(*record);
	}

	return retval;
}

int
od_record_create_cstring(pam_handle_t *pamh, ODRecordRef *record, const char *user)
{
	int retval = PAM_SUCCESS;
	CFStringRef cfUser = NULL;

	if (NULL == record || NULL == user) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	if (PAM_SUCCESS != (retval = cstring_to_cfstring(user, &cfUser)) ||
	    PAM_SUCCESS != (retval = od_record_create(pamh, record, cfUser))) {
		_LOG_DEBUG("od_record_create() failed");
		goto cleanup;
	}

cleanup:
	if (PAM_SUCCESS != retval) {
		_LOG_ERROR("failed: %d", retval);
		if (record != NULL)
			CFReleaseNull(*record);
	}

	CFReleaseSafe(cfUser);

	return retval;
}

/* Can return NULL */
int
od_record_attribute_create_cfarray(ODRecordRef record, CFStringRef attrib,  CFArrayRef *out)
{
	int retval = PAM_SUCCESS;

	if (NULL == record || NULL == attrib || NULL == out) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	*out = ODRecordCopyValues(record, attrib, NULL);

cleanup:
	if (PAM_SUCCESS != retval) {
		CFReleaseSafe(out);
	}
	return retval;
}

/* Can return NULL */
int
od_record_attribute_create_cfstring(ODRecordRef record, CFStringRef attrib,  CFStringRef *out)
{
	int retval = PAM_SERVICE_ERR;
	CFTypeRef cval = NULL;
	CFArrayRef vals = NULL;
	CFIndex i = 0, count = 0;

	if (NULL == record || NULL == attrib || NULL == out) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	*out = NULL;
	retval = od_record_attribute_create_cfarray(record, attrib, &vals);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfarray() failed");
		goto cleanup;
	}
	if (NULL == vals) {
		retval = PAM_SUCCESS;
		goto cleanup;
	}

	count = CFArrayGetCount(vals);
	if (1 != count) {
		char *attr_cstr = NULL;
		cfstring_to_cstring(attrib, &attr_cstr);
		_LOG_DEBUG("returned %lx attributes for %s", count, attr_cstr);
		free(attr_cstr);
	}

	for (i = 0; i < count; ++i) {
		cval = CFArrayGetValueAtIndex(vals, i);
		if (NULL == cval) {
			continue;
		}
		if (CFGetTypeID(cval) == CFStringGetTypeID()) {
			*out = CFStringCreateCopy(kCFAllocatorDefault, cval);
			if (NULL == *out) {
				_LOG_DEBUG("CFStringCreateCopy() failed");
				retval = PAM_BUF_ERR;
				goto cleanup;
			}
			break;
		} else {
			_LOG_DEBUG("attribute is not a cfstring");
			retval = PAM_PERM_DENIED;
			goto cleanup;
		}
	}
	retval = PAM_SUCCESS;

cleanup:
	if (PAM_SUCCESS != retval) {
		CFReleaseSafe(out);
	}
	CFReleaseSafe(vals);

	return retval;
}

/* Can return NULL */
int
od_record_attribute_create_cstring(ODRecordRef record, CFStringRef attrib,  char **out)
{
	int retval = PAM_SERVICE_ERR;
	CFStringRef val = NULL;

	if (NULL == record || NULL == attrib || NULL == out) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	retval = od_record_attribute_create_cfstring(record, attrib, &val);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfstring() failed");
		goto cleanup;
	}

	if (NULL != val) {
		retval = cfstring_to_cstring(val, out);
		if (PAM_SUCCESS != retval) {
			_LOG_DEBUG("cfstring_to_cstring() failed");
			goto cleanup;
		}
	}

cleanup:
	if (PAM_SUCCESS != retval) {
		free(out);
	}

	CFReleaseSafe(val);

	return retval;
}

int
od_record_check_pwpolicy(ODRecordRef record)
{
    CFErrorRef oderror = NULL;
	int retval = PAM_SERVICE_ERR;

	if (NULL == record) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

    if (!ODRecordAuthenticationAllowed(record, &oderror)) {
        switch (CFErrorGetCode(oderror)) {
			case kODErrorCredentialsAccountNotFound:
				retval = PAM_USER_UNKNOWN;
				break;
			case kODErrorCredentialsAccountDisabled:
			case kODErrorCredentialsAccountInactive:
				retval = PAM_PERM_DENIED;
				break;
			case kODErrorCredentialsPasswordExpired:
			case kODErrorCredentialsPasswordChangeRequired:
				retval = PAM_NEW_AUTHTOK_REQD;
				break;
			case kODErrorCredentialsInvalid:
				retval = PAM_AUTH_ERR;
				break;
			case kODErrorCredentialsAccountTemporarilyLocked :
				retval = PAM_APPLE_ACCT_TEMP_LOCK;
				break;
			case kODErrorCredentialsAccountLocked :
				retval = PAM_APPLE_ACCT_LOCKED;
				break;
			default:
				retval = PAM_AUTH_ERR;
				break;
		}

    } else {
        retval = PAM_SUCCESS;
    }

cleanup:
	_LOG_DEBUG("retval: %d", retval);
	return retval;
}

int
od_record_check_authauthority(ODRecordRef record)
{
	int retval = PAM_PERM_DENIED;
	CFStringRef authauth = NULL;

	if (NULL == record) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	retval = od_record_attribute_create_cfstring(record, kODAttributeTypeAuthenticationAuthority, &authauth);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfstring() failed");
		goto cleanup;
	}
	if (NULL == authauth) {
		retval = PAM_SUCCESS;
		goto cleanup;
	}
	if (!CFStringHasPrefix(authauth, CFSTR(kDSValueAuthAuthorityDisabledUser))) {
		retval = PAM_SUCCESS;
	}

cleanup:
	if (PAM_SUCCESS != retval) {
		_LOG_ERROR("failed: %d", retval);
	}

	CFReleaseSafe(authauth);

	return retval;
}

int
od_record_check_homedir(ODRecordRef record)
{
	int retval = PAM_SERVICE_ERR;
	CFStringRef tmp = NULL;

	if (NULL == record) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	retval = od_record_attribute_create_cfstring(record, kODAttributeTypeNFSHomeDirectory, &tmp);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfstring() failed");
		goto cleanup;
	}

	/* Allow NULL home directories */
	if (NULL == tmp) {
		retval = PAM_SUCCESS;
		goto cleanup;
	}

	/* Do not allow login with '/dev/null' home */
	if (kCFCompareEqualTo == CFStringCompare(tmp, CFSTR("/dev/null"), 0)) {
		_LOG_DEBUG("home directory is /dev/null");
		retval = PAM_PERM_DENIED;
		goto cleanup;
	}

	if (kCFCompareEqualTo == CFStringCompare(tmp, CFSTR("99"), 0)) {
		_LOG_DEBUG("home directory is 99");
		retval = PAM_PERM_DENIED;
		goto cleanup;
	}

	retval = PAM_SUCCESS;

cleanup:
	if (PAM_SUCCESS != retval)
		_LOG_ERROR("failed: %d", retval);

	CFReleaseSafe(tmp);

	return retval;
}

int
od_record_check_shell(ODRecordRef record)
{
	int retval = PAM_PERM_DENIED;
	CFStringRef cfstr = NULL;

	if (NULL == record) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	retval = od_record_attribute_create_cfstring(record, kODAttributeTypeUserShell, &cfstr);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfstring() failed");
		goto cleanup;
	}

	if (NULL == cfstr) {
		retval = PAM_SUCCESS;
		goto cleanup;
	}

	if (CFStringCompare(cfstr, CFSTR("/usr/bin/false"), 0) == kCFCompareEqualTo) {
		_LOG_DEBUG("user shell is /bin/false");
		retval = PAM_PERM_DENIED;
	}

cleanup:
	if (PAM_SUCCESS != retval)
		_LOG_ERROR("failed: %d", retval);

	CFReleaseSafe(cfstr);

	return retval;
}

int
od_string_from_record(ODRecordRef record, CFStringRef attrib,  char **out)
{
	int retval = PAM_SERVICE_ERR;
	CFStringRef val = NULL;

	if (NULL == record) {
		_LOG_DEBUG("%s - NULL ODRecord passed.", __func__);
		goto cleanup;
	}

	retval = od_record_attribute_create_cfstring(record, attrib, &val);
	if (PAM_SUCCESS != retval) {
		goto cleanup;
	}

	if (val)
		retval = cfstring_to_cstring(val, out);

cleanup:
	CFReleaseSafe(val);

	return retval;
}

int
extract_homemount(char *in, char **out_url, char **out_path)
{
	// Directory Services people have assured me that this won't change
	static const char URL_OPEN[] = "<url>";
	static const char URL_CLOSE[] = "</url>";
	static const char PATH_OPEN[] = "<path>";
	static const char PATH_CLOSE[] = "</path>";

	char *server_URL = NULL;
	char *path = NULL;
	char *record_start = NULL;
	char *record_end = NULL;

	int retval = PAM_SERVICE_ERR;

	if (NULL == in)
		goto fin;

	record_start = in;
	server_URL = strstr(record_start, URL_OPEN);
	if (NULL == server_URL)
		goto fin;
	server_URL += sizeof(URL_OPEN)-1;
	while ('\0' != *server_URL && isspace(*server_URL))
		server_URL++;
	record_end = strstr(server_URL, URL_CLOSE);
	if (NULL == record_end)
		goto fin;
	while (record_end >= server_URL && '\0' != *record_end && isspace(*(record_end-1)))
		record_end--;
	if (NULL == record_end)
		goto fin;
	*record_end = '\0';
	if (NULL == (*out_url = strdup(server_URL)))
		goto fin;

	record_start = record_end+1;
	path = strstr(record_start, PATH_OPEN);
	if (NULL == path)
		goto ok;
	path += sizeof(PATH_OPEN)-1;
	while ('\0' != *path && isspace(*path))
		path++;
	record_end = strstr(path, PATH_CLOSE);
	if (NULL == record_end)
		goto fin;
	while (record_end >= path && '\0' != *record_end && isspace(*(record_end-1)))
		record_end--;
	if (NULL == record_end)
		goto fin;
	*record_end = '\0';
	if (NULL == (*out_path = strdup(path)))
		goto fin;

ok:
	retval = PAM_SUCCESS;
fin:
	return retval;
}

int
od_extract_home(pam_handle_t *pamh, const char *username, char **server_URL, char **path, char **homedir)
{
	int retval = PAM_SERVICE_ERR;
	char *tmp = NULL;
	ODRecordRef record = NULL;

	retval = od_record_create_cstring(pamh, &record, username);
	if (PAM_SUCCESS != retval) {
		goto cleanup;
	}

	retval = od_string_from_record(record, kODAttributeTypeHomeDirectory, &tmp);
	if (retval) {
		_LOG_DEBUG("%s - get kODAttributeTypeHomeDirectory  : %d",
			    __func__, retval);
		goto cleanup;
	}
	extract_homemount(tmp, server_URL, path);
	_LOG_DEBUG("%s - Server URL   : %s", __func__, *server_URL);
	_LOG_DEBUG("%s - Path to mount: %s", __func__, *path);

	retval = od_string_from_record(record, kODAttributeTypeNFSHomeDirectory, homedir);
	_LOG_DEBUG("%s - Home dir     : %s", __func__, *homedir);
	if (retval)
		goto cleanup;

	retval = PAM_SUCCESS;

cleanup:
	if (tmp)
		free(tmp);
	CFReleaseSafe(record);

	return retval;
}

/* extract the principal from OpenDirectory */
int
od_principal_for_user(pam_handle_t *pamh, const char *user, char **od_principal)
{
	int retval = PAM_SERVICE_ERR;
	ODRecordRef record = NULL;
	CFStringRef principal = NULL;
	CFArrayRef authparts = NULL, vals = NULL;
	CFIndex i = 0, count = 0;

	if (NULL == user || NULL == od_principal) {
		_LOG_DEBUG("NULL argument passed");
		retval = PAM_SERVICE_ERR;
		goto cleanup;
	}

	retval = od_record_create_cstring(pamh, &record, user);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfstring() failed");
		goto cleanup;
	}

	retval = od_record_attribute_create_cfarray(record, kODAttributeTypeAuthenticationAuthority, &vals);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("od_record_attribute_create_cfarray() failed");
		goto cleanup;
	}
	if (NULL == vals) {
		_LOG_DEBUG("no authauth availale for user.");
		retval = PAM_PERM_DENIED;
		goto cleanup;
	}

	count = CFArrayGetCount(vals);
	for (i = 0; i < count; i++)
	{
		const void *val = CFArrayGetValueAtIndex(vals, i);
		if (NULL == val || CFGetTypeID(val) != CFStringGetTypeID())
			break;

		authparts = CFStringCreateArrayBySeparatingStrings(kCFAllocatorDefault, val, CFSTR(";"));
		if (NULL == authparts)
			continue;

		if ((CFArrayGetCount(authparts) < 5) ||
		    (CFStringCompare(CFArrayGetValueAtIndex(authparts, 1), CFSTR("Kerberosv5"), kCFCompareEqualTo)) ||
		    (CFStringHasPrefix(CFArrayGetValueAtIndex(authparts, 4), CFSTR("LKDC:")))) {
			if (NULL != authparts) {
				CFRelease(authparts);
				authparts = NULL;
			}
			continue;
		} else {
			break;
		}
	}

	if (NULL == authparts) {
		_LOG_DEBUG("No authentication authority returned");
		retval = PAM_PERM_DENIED;
		goto cleanup;
	}

	principal = CFArrayGetValueAtIndex(authparts, 3);
	if (NULL == principal) {
		_LOG_DEBUG("no principal found in authentication authority");
		retval = PAM_PERM_DENIED;
		goto cleanup;
	}

	retval = cfstring_to_cstring(principal, od_principal);
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("cfstring_to_cstring() failed");
		goto cleanup;
	}


cleanup:
	if (PAM_SUCCESS != retval) {
		_LOG_DEBUG("failed: %d", retval);
	}

	CFReleaseSafe(record);
	CFReleaseSafe(authparts);
	CFReleaseSafe(vals);

	return retval;
}

void
pam_cf_cleanup(__unused pam_handle_t *pamh, void *data, __unused int pam_end_status)
{
	if (data) {
		CFStringRef *cfstring = data;
		CFRelease(*cfstring);
	}
}
