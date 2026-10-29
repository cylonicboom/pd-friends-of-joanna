/**
 * The intercom (board c-net-api-port): pd's local-only UDP command port.
 *
 * This is the one local channel between pd and the services that fill its
 * seam (the sidecar on a dedicated box, nothing else). It speaks the console
 * grammar in and JSON out, so pd vendors no parser:
 *
 *   in   <token> <id> <console line>        one command per datagram
 *   out  {"id":<id>,"ok":<bool>,"text":"…"} one reply per command, the
 *                                          console's lines joined by \n
 *   subscribe                              handled here: the sender gets
 *                                          events for the next 30 s and
 *                                          re-subscribes to keep them
 *   event {"event":"<name>",…}             pushed to the subscriber, no id
 *
 * Off unless --api-port N (0 = any free port) or Net.Api.Port is set. Binds 127.0.0.1 only
 * (the sidecar connects there), and any local process can reach a loopback
 * port, so every datagram carries a token: pd mints one at startup and
 * writes {port, token, pid, proto} to $S/api.json (mode 0600) for the
 * sidecar to read. A datagram with the wrong token gets no reply at all.
 *
 * Lines run through netConsoleExecFrom with the API as their origin, so
 * what the intercom may run is Api.Allow's business (netconsole.c), not
 * this file's. Polled once a frame, a bounded number of datagrams per
 * frame, no threads. fojo-sock's sockcore/intercom.rs is the other end;
 * change the framing in both places or neither.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "types.h"
#include "config.h"
#include "system.h"
#include "fs.h"

#define NETAPI_MAX_DATAGRAM 4096
#define NETAPI_PER_FRAME    16
#define NETAPI_LEASE_MS     30000
#define NETAPI_TOKEN_BYTES  16
#define NETAPI_FILE         "$S/api.json"

u32 g_NetApiPort = 0; // Net.Api.Port / --api-port; 0 = the intercom is off

static s32 s_apiInit = 0;
static ENetSocket s_apiSock = ENET_SOCKET_NULL;
static u16 s_apiBoundPort = 0;
static char s_apiToken[NETAPI_TOKEN_BYTES * 2 + 1];

// the one subscriber: whoever last said `subscribe`, until its lease runs out
static ENetAddress s_subAddr;
static u32 s_subUntil = 0; // enet_time_get() ms; 0 = nobody

// the reply being built for the line running right now
static char s_replyText[NETAPI_MAX_DATAGRAM];
static s32 s_replyLen = 0;

static s32 s_badTokenLogged = 0;

static void netApiLoopback(ENetAddress *addr, u16 port)
{
	// 127.0.0.1 as enet's v4-mapped form, on the dual-stack socket
	memset(addr, 0, sizeof(*addr));
	addr->ipv4.ffff = 0xffff;
	addr->ipv4.ip.s_addr = htonl(0x7f000001u);
	addr->port = port;
}

static s32 netApiRandom(u8 *out, s32 n)
{
#ifdef _WIN32
	for (s32 i = 0; i < n; i += sizeof(unsigned int)) {
		unsigned int r;
		if (rand_s(&r) != 0) {
			return 0;
		}
		memcpy(out + i, &r, (n - i) < (s32)sizeof(r) ? (n - i) : (s32)sizeof(r));
	}
	return 1;
#else
	const int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		return 0;
	}
	s32 got = 0;
	while (got < n) {
		const ssize_t r = read(fd, out + got, n - got);
		if (r <= 0) {
			break;
		}
		got += (s32)r;
	}
	close(fd);
	return got == n;
#endif
}

static void netApiWriteInfo(void)
{
	const char *path = fsFullPath(NETAPI_FILE);
	char body[256];
	const s32 n = snprintf(body, sizeof(body), "{\"port\":%u,\"token\":\"%s\",\"pid\":%ld,\"proto\":%d}\n",
			s_apiBoundPort, s_apiToken, (long)
#ifdef _WIN32
			_getpid()
#else
			getpid()
#endif
			, NET_PROTOCOL_VER);

#ifndef _WIN32
	// 0600 from the first byte: the token is what keeps other local users off the port
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || write(fd, body, (size_t)n) != n) {
		sysLogPrintf(LOG_ERROR, "NET: api: could not write %s", path);
	}
	if (fd >= 0) {
		close(fd);
	}
#else
	FILE *f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "NET: api: could not write %s", path);
		return;
	}
	fwrite(body, 1, (size_t)n, f);
	fclose(f);
#endif
}

static void netApiOpen(void)
{
	ENetAddress addr;
	u8 raw[NETAPI_TOKEN_BYTES];

	if (!netApiRandom(raw, sizeof(raw))) {
		sysLogPrintf(LOG_ERROR, "NET: api: no random source for the token; intercom stays off");
		return;
	}
	for (s32 i = 0; i < NETAPI_TOKEN_BYTES; i++) {
		snprintf(s_apiToken + i * 2, 3, "%02x", raw[i]);
	}

	s_apiSock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (s_apiSock == ENET_SOCKET_NULL) {
		sysLogPrintf(LOG_ERROR, "NET: api: socket create failed");
		return;
	}
	enet_socket_set_option(s_apiSock, ENET_SOCKOPT_IPV6_V6ONLY, 0);
	enet_socket_set_option(s_apiSock, ENET_SOCKOPT_NONBLOCK, 1);

	netApiLoopback(&addr, (u16)g_NetApiPort);
	if (enet_socket_bind(s_apiSock, &addr) < 0) {
		sysLogPrintf(LOG_ERROR, "NET: api: bind 127.0.0.1:%u failed", g_NetApiPort);
		enet_socket_destroy(s_apiSock);
		s_apiSock = ENET_SOCKET_NULL;
		return;
	}
	if (enet_socket_get_address(s_apiSock, &addr) == 0) {
		s_apiBoundPort = addr.port;
	} else {
		s_apiBoundPort = (u16)g_NetApiPort;
	}

	netApiWriteInfo();
	sysLogPrintf(LOG_NOTE, "NET: api: intercom on 127.0.0.1:%u, token in %s", s_apiBoundPort, NETAPI_FILE);
}

static void netApiSendTo(const ENetAddress *to, const char *text, s32 len)
{
	ENetBuffer buf;
	buf.data = (void *)text;
	buf.dataLength = (size_t)len;
	enet_socket_send(s_apiSock, to, &buf, 1);
}

// append `s` to `out` as the inside of a JSON string; returns the new length
s32 netApiJsonEscape(char *out, s32 outlen, s32 at, const char *s)
{
	for (; *s && at < outlen - 7; s++) {
		const unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			out[at++] = '\\';
			out[at++] = (char)c;
		} else if (c == '\n') {
			out[at++] = '\\';
			out[at++] = 'n';
		} else if (c < 0x20) {
			at += snprintf(out + at, outlen - at, "\\u%04x", c);
		} else {
			out[at++] = (char)c;
		}
	}
	out[at] = '\0';
	return at;
}

static void netApiReply(const ENetAddress *to, unsigned long long id, s32 ok, const char *text)
{
	static char dgram[NETAPI_MAX_DATAGRAM];
	s32 n = snprintf(dgram, sizeof(dgram), "{\"id\":%llu,\"ok\":%s,\"text\":\"", id, ok ? "true" : "false");
	n = netApiJsonEscape(dgram, sizeof(dgram) - 3, n, text);
	n += snprintf(dgram + n, sizeof(dgram) - n, "\"}");
	netApiSendTo(to, dgram, n);
}

// netConPrintf's sink for the API origin: collect the lines, send once
static void netApiReplyLine(void *ctx, const char *line)
{
	(void)ctx;
	if (s_replyLen && s_replyLen < (s32)sizeof(s_replyText) - 1) {
		s_replyText[s_replyLen++] = '\n';
	}
	const s32 room = (s32)sizeof(s_replyText) - 1 - s_replyLen;
	if (room > 0) {
		const s32 n = snprintf(s_replyText + s_replyLen, room + 1, "%s", line);
		s_replyLen += n < room ? n : room;
	}
}

/**
 * Push an event to the subscriber, if there is one with a live lease.
 * `fields` is the rest of the object, already JSON, without the braces
 * ("\"joinid\":9,\"name\":\"x\""), or NULL. Nothing is queued: a subscriber
 * that is down misses it, and the files pd writes anyway are the record.
 */
void netApiEvent(const char *event, const char *fields)
{
	static char dgram[NETAPI_MAX_DATAGRAM];

	if (s_apiSock == ENET_SOCKET_NULL || !s_subUntil) {
		return;
	}
	if ((s32)(enet_time_get() - s_subUntil) > 0) {
		s_subUntil = 0;
		return;
	}
	const s32 n = snprintf(dgram, sizeof(dgram), "{\"event\":\"%s\"%s%s}", event, fields && *fields ? "," : "", fields ? fields : "");
	netApiSendTo(&s_subAddr, dgram, n);
}

s32 netApiIsOn(void)
{
	return s_apiSock != ENET_SOCKET_NULL;
}

static void netApiHandle(const ENetAddress *from, char *msg)
{
	// <token> <id> <line>
	char *tok = msg;
	char *sp = strchr(tok, ' ');
	if (!sp) {
		return;
	}
	*sp = '\0';
	char *idstr = sp + 1;
	char *end;
	const unsigned long long id = strtoull(idstr, &end, 10);
	if (end == idstr || (*end != ' ' && *end != '\0')) {
		return;
	}
	char *line = *end ? end + 1 : end;

	if (!netSecureStrEqual(s_apiToken, tok)) {
		// no reply: a probe learns nothing, not even that the port is pd's
		if (!s_badTokenLogged) {
			s_badTokenLogged = 1;
			sysLogPrintf(LOG_WARNING, "NET: api: a datagram with the wrong token (logged once)");
		}
		return;
	}

	while (*line == ' ') {
		line++;
	}

	if (strcmp(line, "subscribe") == 0) {
		s_subAddr = *from;
		s_subUntil = enet_time_get() + NETAPI_LEASE_MS;
		if (!s_subUntil) {
			s_subUntil = 1;
		}
		netApiReply(from, id, 1, "subscribed " "30" "s");
		return;
	}

	struct netconorigin origin = { NETCON_API, 0, NULL, netApiReplyLine, NULL, 1 };
	s_replyLen = 0;
	s_replyText[0] = '\0';
	netConsoleExecFrom(line, &origin);
	netApiReply(from, id, origin.ok, s_replyText);
}

/**
 * Once a frame from pdsched, any mode: open on first call if configured,
 * then take up to NETAPI_PER_FRAME datagrams.
 */
void netApiTick(void)
{
	if (!s_apiInit) {
		s_apiInit = 1;
		// --api-port N turns it on at N; --api-port 0 is on at any free port
		// (api.json says which). Net.Api.Port alone: 0 stays off.
		s32 on = g_NetApiPort != 0;
		if (sysArgCheck("--api-port")) {
			const s32 argport = sysArgGetInt("--api-port", 0);
			g_NetApiPort = (argport > 0 && argport <= 0xFFFF) ? (u32)argport : 0;
			on = 1;
		}
		if (on) {
			netApiOpen();
		}
	}
	if (s_apiSock == ENET_SOCKET_NULL) {
		return;
	}

	for (s32 i = 0; i < NETAPI_PER_FRAME; i++) {
		static char msg[NETAPI_MAX_DATAGRAM];
		ENetAddress from;
		ENetBuffer buf;
		buf.data = msg;
		buf.dataLength = sizeof(msg) - 1;
		const int n = enet_socket_receive(s_apiSock, &from, &buf, 1);
		if (n <= 0) {
			return;
		}
		msg[n] = '\0';
		// loopback only, whatever the socket lets through
		if (from.ipv4.ffff != 0xffff || from.ipv4.ip.s_addr != htonl(0x7f000001u)) {
			continue;
		}
		netApiHandle(&from, msg);
	}
}

PD_CONSTRUCTOR static void netApiConfigInit(void)
{
	configRegisterUInt("Net.Api.Port", &g_NetApiPort, 0, 0xFFFF);
}
