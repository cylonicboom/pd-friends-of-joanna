/**
 * Join verification (board c-net-api-port / c-net-two-tiers): the hold in
 * CLSTATE_AUTH while the sidecar decides.
 *
 * Net.Verify = off | required. off is kai's behaviour: CLC_AUTH that passes
 * the password and the mod handshake is admitted on the spot. required: the
 * client stays parked in CLSTATE_AUTH (where kai's connect path already put
 * it - there is no app-level timer on either side, and CLC_SETTINGS is not
 * state-gated, so what arrives meanwhile still applies), pd pushes a `join`
 * event on the intercom carrying the ticket the client brought, and nothing
 * moves until a `verdict <joinid> accept|deny` comes back. No verdict
 * within Net.Verify.Timeout seconds is a deny: public servers fail closed
 * (her 10-25 ruling); deploy restarts the sidecar so that lockout lasts
 * seconds, and seated players are never touched by any of this.
 *
 * pd never reads the ticket. It is bytes from --ticket, handed to the
 * sidecar, which answers with an opaque account principal that pd stores
 * and byte-compares. account is set here and nowhere else.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "types.h"
#include "config.h"
#include "system.h"
#include "fs.h"

char g_NetVerifyCfg[16] = "off";     // Net.Verify: off | required
u32 g_NetVerifyTimeoutSecs = 10;      // Net.Verify.Timeout
char g_NetJoinTicket[NET_TICKET_LEN]; // --ticket: what this client hands the server

static u32 s_nextJoinId = 1;

s32 netVerifyRequired(void)
{
	return strcasecmp(g_NetVerifyCfg, "required") == 0
			|| strcasecmp(g_NetVerifyCfg, "on") == 0
			|| strcmp(g_NetVerifyCfg, "1") == 0;
}

/**
 * --ticket <file>: the helper's ticket.json, or a file holding the bare
 * ticket string. No JSON parser: a file that starts with '{' is scanned for
 * "ticket":" and the string up to the next quote is taken; anything else is
 * the ticket itself, trimmed.
 */
void netVerifyLoadTicket(const char *path)
{
	g_NetJoinTicket[0] = '\0';
	if (!path || !*path) {
		return;
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "NET: --ticket: cannot open %s", path);
		return;
	}
	static char buf[NET_TICKET_LEN * 4];
	const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';

	const char *s = buf;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
		s++;
	}
	const char *e;
	if (*s == '{') {
		const char *k = strstr(s, "\"ticket\"");
		if (!k) {
			sysLogPrintf(LOG_WARNING, "NET: --ticket: no \"ticket\" in %s", path);
			return;
		}
		k = strchr(k + 8, '"');
		if (!k) {
			return;
		}
		s = k + 1;
		e = strchr(s, '"');
		if (!e) {
			return;
		}
	} else {
		e = s;
		while (*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n') {
			e++;
		}
	}
	const size_t len = (size_t)(e - s);
	if (len >= sizeof(g_NetJoinTicket)) {
		sysLogPrintf(LOG_WARNING, "NET: --ticket: ticket longer than %d bytes, not sent", NET_TICKET_LEN - 1);
		return;
	}
	memcpy(g_NetJoinTicket, s, len);
	g_NetJoinTicket[len] = '\0';
	sysLogPrintf(LOG_NOTE, "NET: --ticket: %d bytes from %s", (s32)len, path);
}

// deny with a reason the player sees: SVC_DENIED carries the text, then the
// disconnect code names the category
static void netVerifyDeny(struct netclient *cl, const char *reason)
{
	sysLogPrintf(LOG_NOTE, "NET: verify: join %u (%s) denied: %s", cl->joinid, cl->settings.name, reason);
	netbufStartWrite(&cl->out);
	netmsgSvcDeniedWrite(&cl->out, reason);
	netSend(cl, NULL, true, NETCHAN_CONTROL);
	netServerKickAfterSend(cl, DISCONNECT_VERIFY);
	cl->verifyheld = 0;
}

/**
 * Called from netmsgClcAuthRead once the password and the mods have passed
 * and the name is stored. Returns 1 when the client was held (the caller
 * must not admit), 0 to admit now.
 */
s32 netVerifyHold(struct netclient *cl, const char *ticket)
{
	if (!netVerifyRequired()) {
		return 0;
	}

	cl->joinid = s_nextJoinId++;
	if (!s_nextJoinId) {
		s_nextJoinId = 1;
	}
	cl->verifyheld = 1;
	cl->holdsince = enet_time_get();

	static char fields[NET_TICKET_LEN * 2 + 256];
	s32 n = snprintf(fields, sizeof(fields), "\"joinid\":%u,\"name\":\"", cl->joinid);
	n = netApiJsonEscape(fields, sizeof(fields), n, cl->settings.name);
	n += snprintf(fields + n, sizeof(fields) - n, "\",\"uuid\":\"");
	n = netApiJsonEscape(fields, sizeof(fields), n, cl->uuid);
	n += snprintf(fields + n, sizeof(fields) - n, "\",\"ticket\":\"");
	n = netApiJsonEscape(fields, sizeof(fields), n, ticket ? ticket : "");
	snprintf(fields + n, sizeof(fields) - n, "\"");

	sysLogPrintf(LOG_NOTE, "NET: verify: holding client %u (%s) as join %u%s", cl->id, cl->settings.name,
			cl->joinid, netApiIsOn() ? "" : " - no intercom, this will time out");
	netApiEvent("join", fields);
	return 1;
}

/**
 * The verdict verb's work. line is what follows "verdict ":
 *   <joinid> accept|deny [account:<x>] [owns-uuid] [reason...]
 * Writes a one-line result to out; returns 1 on success, 0 on a bad line
 * or an unknown join (the console reports ok:false).
 */
s32 netVerifyVerdict(const char *line, char *out, s32 outlen)
{
	char *end;
	const unsigned long id = strtoul(line, &end, 10);
	if (end == line || id == 0) {
		snprintf(out, outlen, "verdict: <joinid> accept|deny [account:<x>] [owns-uuid] [reason]");
		return 0;
	}
	while (*end == ' ') {
		end++;
	}
	s32 accept;
	if (strncmp(end, "accept", 6) == 0 && (end[6] == ' ' || end[6] == '\0')) {
		accept = 1;
		end += 6;
	} else if (strncmp(end, "deny", 4) == 0 && (end[4] == ' ' || end[4] == '\0')) {
		accept = 0;
		end += 4;
	} else {
		snprintf(out, outlen, "verdict: expected accept or deny");
		return 0;
	}

	struct netclient *cl = NULL;
	for (s32 i = 1; i < NET_MAX_CLIENTS; i++) {
		if (g_NetClients[i].state == CLSTATE_AUTH && g_NetClients[i].verifyheld
				&& g_NetClients[i].joinid == (u32)id) {
			cl = &g_NetClients[i];
			break;
		}
	}
	if (!cl) {
		snprintf(out, outlen, "verdict: no held join %lu", id);
		return 0;
	}

	char account[NET_ACCOUNT_LEN] = "";
	s32 owns = 0;
	const char *reason = NULL;
	const char *p = end;
	while (*p) {
		while (*p == ' ') {
			p++;
		}
		if (!*p) {
			break;
		}
		if (strncmp(p, "account:", 8) == 0) {
			const char *v = p + 8;
			const char *ve = v;
			while (*ve && *ve != ' ') {
				ve++;
			}
			const size_t len = (size_t)(ve - v);
			if (len == 0 || len >= sizeof(account)) {
				snprintf(out, outlen, "verdict: account principal empty or longer than %d", NET_ACCOUNT_LEN - 1);
				return 0;
			}
			memcpy(account, v, len);
			account[len] = '\0';
			p = ve;
		} else if (strncmp(p, "owns-uuid", 9) == 0 && (p[9] == ' ' || p[9] == '\0')) {
			owns = 1;
			p += 9;
		} else {
			reason = p; // the rest of the line
			break;
		}
	}

	if (accept) {
		strcpy(cl->account, account);
		cl->uuidowned = (u8)owns;
		cl->verifyheld = 0;
		sysLogPrintf(LOG_NOTE, "NET: verify: join %u (%s) accepted%s%s%s", cl->joinid, cl->settings.name,
				account[0] ? " as " : "", account, owns ? ", owns its uuid" : "");
		netServerAdmit(cl);
		snprintf(out, outlen, "admitted #%u %s%s%s", cl->id, cl->settings.name, account[0] ? " as " : "", account);
	} else {
		netVerifyDeny(cl, reason && *reason ? reason : "sign-in refused");
		snprintf(out, outlen, "denied #%u %s", cl->id, cl->settings.name);
	}
	return 1;
}

/**
 * Once a frame on a server: deny every held join past the timeout.
 */
void netVerifyTick(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}
	const u32 now = enet_time_get();
	for (s32 i = 1; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state == CLSTATE_AUTH && cl->verifyheld
				&& now - cl->holdsince > g_NetVerifyTimeoutSecs * 1000u) {
			netVerifyDeny(cl, "no verdict - the server could not verify you in time");
		}
	}
}

PD_CONSTRUCTOR static void netVerifyConfigInit(void)
{
	configRegisterString("Net.Verify", g_NetVerifyCfg, sizeof(g_NetVerifyCfg) - 1);
	configRegisterUInt("Net.Verify.Timeout", &g_NetVerifyTimeoutSecs, 1, 600);
}
