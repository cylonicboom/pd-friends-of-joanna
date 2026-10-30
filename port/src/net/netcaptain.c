/**
 * The standing list (board c-net-two-tiers, slice 3): who is a standing
 * captain on this server, by principal.
 *
 * A principal is a short prefixed string pd compares byte for byte:
 *
 *   pk:<base64 of the 32-byte ed25519 public key>   the captain key
 *                                                   (netcaptainkey.c) the
 *                                                   client PROVED on join
 *   account:<opaque>                                 whatever the verdict put
 *                                                   in cl->account; a DID on
 *                                                   the atproto tier
 *
 * `uuid:` entries (or a bare uuid) are refused at load: a pak uuid is an
 * identifier that ends up on profile pages, never a credential.
 *
 * Two sources, read in union, no tier flag anywhere:
 *
 *   Captain.Standing      in pd.ini, the operator's hand list, principals
 *                         separated by commas or whitespace. `standing add`
 *                         and `standing rm` edit this one.
 *   Captain.StandingFile  a plain file, one principal per line, written
 *                         atomically by the sidecar from the operator's
 *                         record. pd never writes it. Re-read on `standing
 *                         reload` (the sidecar's nudge over the intercom) or
 *                         when its mtime changes.
 *
 * The one deliverable here is netCaptainIsStanding(). It grants nothing yet
 * (rights and the election are c-net-captain's); it is visible as the
 * `CAPTAIN standing ...` diary line when such a player is admitted, in
 * `players`, and as standing= on the netdiag netcl line.
 *
 * On a server with Net.Verify=off nothing ever sets cl->account, so every
 * account: entry is inert; that is logged once at load so the operator
 * knows why.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "types.h"
#include "config.h"
#include "system.h"
#include "fs.h"

#define STANDING_MAX       64
#define STANDING_PK_LEN    44   // netPkBase64: 43 chars + '='
#define STANDING_VAL_LEN   NET_ACCOUNT_LEN

char g_NetCaptainStanding[1024] = "";     // Captain.Standing
char g_NetCaptainStandingFile[256] = "";  // Captain.StandingFile

enum { STANDING_PK = 1, STANDING_ACCOUNT = 2 };

struct standingentry {
	u8 kind;
	u8 fromfile;
	char val[STANDING_VAL_LEN]; // pk: the base64 without its '=' pad; account: as typed
};

static struct standingentry s_entries[STANDING_MAX];
static s32 s_count = 0;
static s32 s_loaded = 0;
static s32 s_inertLogged = 0;
static time_t s_fileMtime = 0;
static u32 s_nextStat = 0;

static void netCaptainPkNoPad(const u8 pk[32], char out[STANDING_PK_LEN + 1])
{
	netPkBase64(pk, out);
	// the '=' pad carries no bits; compare without it so either spelling matches
	for (char *p = out; *p; p++) {
		if (*p == '=') {
			*p = '\0';
			break;
		}
	}
}

static s32 netCaptainB64Char(char c)
{
	return isalnum((unsigned char)c) || c == '+' || c == '/';
}

/**
 * Parse one principal into an entry. 0 and a reason when it is not one pd
 * will take. `src` is only for the log line.
 */
static s32 netCaptainParse(const char *text, struct standingentry *out, const char *src)
{
	memset(out, 0, sizeof(*out));

	if (strncmp(text, "pk:", 3) == 0) {
		const char *b = text + 3;
		size_t n = strlen(b);
		if (n == STANDING_PK_LEN && b[n - 1] == '=') {
			n--;
		}
		if (n != STANDING_PK_LEN - 1) {
			sysLogPrintf(LOG_WARNING, "NET: standing (%s): pk: entry is not 43 base64 chars, ignored: %s", src, text);
			return 0;
		}
		for (size_t i = 0; i < n; i++) {
			if (!netCaptainB64Char(b[i])) {
				sysLogPrintf(LOG_WARNING, "NET: standing (%s): pk: entry is not base64, ignored: %s", src, text);
				return 0;
			}
		}
		out->kind = STANDING_PK;
		memcpy(out->val, b, n);
		out->val[n] = '\0';
		return 1;
	}

	if (strncmp(text, "account:", 8) == 0) {
		const char *a = text + 8;
		if (!*a || strlen(a) >= STANDING_VAL_LEN) {
			sysLogPrintf(LOG_WARNING, "NET: standing (%s): account: entry empty or too long, ignored", src);
			return 0;
		}
		out->kind = STANDING_ACCOUNT;
		snprintf(out->val, sizeof(out->val), "%s", a);
		return 1;
	}

	if (strncmp(text, "uuid:", 5) == 0 || (strlen(text) == 36 && text[8] == '-' && text[13] == '-')) {
		sysLogPrintf(LOG_WARNING, "NET: standing (%s): a pak uuid is an identifier, never a credential - refused: %s", src, text);
		return 0;
	}

	sysLogPrintf(LOG_WARNING, "NET: standing (%s): not a principal (pk:... or account:...), ignored: %s", src, text);
	return 0;
}

static void netCaptainAddEntry(const struct standingentry *e)
{
	for (s32 i = 0; i < s_count; i++) {
		if (s_entries[i].kind == e->kind && strcmp(s_entries[i].val, e->val) == 0) {
			return; // in both sources, listed once
		}
	}
	if (s_count >= STANDING_MAX) {
		sysLogPrintf(LOG_WARNING, "NET: standing: more than %d entries, the rest are ignored", STANDING_MAX);
		return;
	}
	s_entries[s_count++] = *e;
}

// split `list` on commas and whitespace, parse each token
static void netCaptainLoadList(const char *list, s32 fromfile, const char *src)
{
	char tok[STANDING_VAL_LEN + 16];
	const char *p = list;

	while (*p) {
		while (*p && (*p == ',' || isspace((unsigned char)*p))) {
			p++;
		}
		if (!*p) {
			break;
		}
		s32 n = 0;
		while (*p && *p != ',' && !isspace((unsigned char)*p)) {
			if (n < (s32)sizeof(tok) - 1) {
				tok[n++] = *p;
			}
			p++;
		}
		tok[n] = '\0';
		if (tok[0] == '#') {
			// a comment runs to the end of the line
			while (*p && *p != '\n') {
				p++;
			}
			continue;
		}
		struct standingentry e;
		if (netCaptainParse(tok, &e, src)) {
			e.fromfile = (u8)fromfile;
			netCaptainAddEntry(&e);
		}
	}
}

static const char *netCaptainFilePath(void)
{
	if (!g_NetCaptainStandingFile[0]) {
		return NULL;
	}
	return fsFullPath(g_NetCaptainStandingFile);
}

static time_t netCaptainFileMtime(void)
{
	const char *path = netCaptainFilePath();
	struct stat st;
	if (!path || stat(path, &st) != 0) {
		return 0;
	}
	return st.st_mtime;
}

/**
 * Rebuild the list from both sources. Returns the entry count. Logs what it
 * refused and, once, that account: entries are inert without verification.
 */
s32 netCaptainStandingReload(void)
{
	s_count = 0;
	s_loaded = 1;

	netCaptainLoadList(g_NetCaptainStanding, 0, "Captain.Standing");

	const char *path = netCaptainFilePath();
	s32 fromfile = 0;
	if (path) {
		FILE *f = fopen(path, "rb");
		if (f) {
			static char buf[16384];
			const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n] = '\0';
			fclose(f);
			const s32 before = s_count;
			netCaptainLoadList(buf, 1, "Captain.StandingFile");
			fromfile = s_count - before;
		} else {
			sysLogPrintf(LOG_NOTE, "NET: standing: Captain.StandingFile %s is not there yet; ini list only", g_NetCaptainStandingFile);
		}
		s_fileMtime = netCaptainFileMtime();
	}

	s32 accounts = 0;
	for (s32 i = 0; i < s_count; i++) {
		accounts += s_entries[i].kind == STANDING_ACCOUNT;
	}
	if (accounts && !netVerifyRequired() && !s_inertLogged) {
		s_inertLogged = 1;
		sysLogPrintf(LOG_NOTE, "NET: standing: %d account: entr%s inert without verification (Net.Verify=off: nothing ever sets a client's account)",
				accounts, accounts == 1 ? "y is" : "ies are");
	}

	sysLogPrintf(LOG_NOTE, "NET: standing: %d entr%s (%d from the file)", s_count, s_count == 1 ? "y" : "ies", fromfile);
	return s_count;
}

static void netCaptainEnsureLoaded(void)
{
	if (!s_loaded) {
		netCaptainStandingReload();
	}
}

/**
 * Once a frame: re-read the standing file when its mtime moved (the sidecar
 * wrote it and the nudge was lost, or the operator edited it by hand). A
 * stat a second, only when a file is configured.
 */
void netCaptainStandingTick(void)
{
	if (g_NetMode != NETMODE_SERVER || !g_NetCaptainStandingFile[0]) {
		return;
	}
	netCaptainEnsureLoaded();
	const u32 now = enet_time_get();
	if ((s32)(now - s_nextStat) < 0) {
		return;
	}
	s_nextStat = now + 1000;
	const time_t m = netCaptainFileMtime();
	if (m != s_fileMtime) {
		sysLogPrintf(LOG_NOTE, "NET: standing: Captain.StandingFile changed, reloading");
		netCaptainStandingReload();
	}
}

/**
 * The strongest principal this client has: account: when a verdict set one,
 * else pk: when the join signature verified, else "" (and 0).
 */
s32 netCaptainPrincipalFor(const struct netclient *cl, char *out, s32 outlen)
{
	if (cl->account[0]) {
		snprintf(out, outlen, "account:%s", cl->account);
		return 1;
	}
	if (cl->haspk) {
		char b64[STANDING_PK_LEN + 1];
		netCaptainPkNoPad(cl->pk, b64);
		snprintf(out, outlen, "pk:%s", b64);
		return 1;
	}
	out[0] = '\0';
	return 0;
}

/** Is this client on the standing list? The predicate the captain system keys on. */
s32 netCaptainIsStanding(const struct netclient *cl)
{
	netCaptainEnsureLoaded();

	char b64[STANDING_PK_LEN + 1];
	s32 havepk = 0;

	for (s32 i = 0; i < s_count; i++) {
		const struct standingentry *e = &s_entries[i];
		if (e->kind == STANDING_PK && cl->haspk) {
			if (!havepk) {
				netCaptainPkNoPad(cl->pk, b64);
				havepk = 1;
			}
			if (strcmp(e->val, b64) == 0) {
				return 1;
			}
		} else if (e->kind == STANDING_ACCOUNT && cl->account[0]) {
			if (strcmp(e->val, cl->account) == 0) {
				return 1;
			}
		}
	}
	return 0;
}

/**
 * Edit the ini list. add: 1 added, 0 already there, -1 refused (not a
 * principal, or the list is full). rm: 1 removed, 0 not in the ini list
 * (a file entry can only go by the file).
 */
s32 netCaptainStandingAdd(const char *principal)
{
	struct standingentry e;
	if (!netCaptainParse(principal, &e, "standing add")) {
		return -1;
	}
	// already in the ini list?
	char tmp[sizeof(g_NetCaptainStanding)];
	snprintf(tmp, sizeof(tmp), "%s", g_NetCaptainStanding);
	for (char *t = strtok(tmp, ", \t\r\n"); t; t = strtok(NULL, ", \t\r\n")) {
		struct standingentry have;
		if (netCaptainParse(t, &have, "Captain.Standing") && have.kind == e.kind && strcmp(have.val, e.val) == 0) {
			return 0;
		}
	}
	const size_t cur = strlen(g_NetCaptainStanding);
	const size_t add = strlen(principal) + (cur ? 1 : 0);
	if (cur + add >= sizeof(g_NetCaptainStanding)) {
		sysLogPrintf(LOG_WARNING, "NET: standing add: Captain.Standing is full");
		return -1;
	}
	if (cur) {
		strcat(g_NetCaptainStanding, ",");
	}
	strcat(g_NetCaptainStanding, principal);
	netCaptainStandingReload();
	return 1;
}

s32 netCaptainStandingRm(const char *principal)
{
	struct standingentry e;
	if (!netCaptainParse(principal, &e, "standing rm")) {
		return -1;
	}
	char tmp[sizeof(g_NetCaptainStanding)];
	char rebuilt[sizeof(g_NetCaptainStanding)] = "";
	s32 removed = 0;
	snprintf(tmp, sizeof(tmp), "%s", g_NetCaptainStanding);
	for (char *t = strtok(tmp, ", \t\r\n"); t; t = strtok(NULL, ", \t\r\n")) {
		struct standingentry have;
		if (netCaptainParse(t, &have, "Captain.Standing") && have.kind == e.kind && strcmp(have.val, e.val) == 0) {
			removed = 1;
			continue;
		}
		if (rebuilt[0]) {
			strcat(rebuilt, ",");
		}
		strcat(rebuilt, t);
	}
	if (removed) {
		snprintf(g_NetCaptainStanding, sizeof(g_NetCaptainStanding), "%s", rebuilt);
		netCaptainStandingReload();
	}
	return removed;
}

/** For `standing list`: entry i as "pk:..." / "account:..." and whether it came from the file. */
s32 netCaptainStandingEntry(s32 i, char *out, s32 outlen, s32 *fromfile)
{
	netCaptainEnsureLoaded();
	if (i < 0 || i >= s_count) {
		return 0;
	}
	snprintf(out, outlen, "%s%s", s_entries[i].kind == STANDING_PK ? "pk:" : "account:", s_entries[i].val);
	*fromfile = s_entries[i].fromfile;
	return 1;
}

PD_CONSTRUCTOR static void netCaptainConfigInit(void)
{
	configRegisterString("Captain.Standing", g_NetCaptainStanding, sizeof(g_NetCaptainStanding));
	configRegisterString("Captain.StandingFile", g_NetCaptainStandingFile, sizeof(g_NetCaptainStandingFile));
}
