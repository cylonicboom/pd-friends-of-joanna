/**
 * The captain key (board c-net-two-tiers, "the chore where a pak gets a
 * password"): one ed25519 keypair per pak, so a player without a Bluesky
 * account can still be named as a standing captain by something a copy of
 * the wire cannot reuse. A pak UUID is an identifier and never a credential;
 * this is the credential.
 *
 * The seed (32 bytes, hex) lives in pd.ini as Pak.<serial>.CaptainKey, keyed
 * on the serial of the pak the local player's file lives on (slot 0's guid)
 * until c-pak-uuid gives paks a UUID entry to sit next to. It is minted from
 * the OS random source the first time it is needed and never leaves this
 * machine. `captain key new` rotates it.
 *
 * The handshake is textbook challenge-response, no crypto of ours:
 *   server -> client  SVC_CHALLENGE {nonce[32], serverid[32]}  at connect
 *   client -> server  CLC_AUTH ... {u8 haskey, pk[32], sig[64]}
 * where sig = ed25519(seed, nonce || serverid). The nonce stops a replay of
 * a capture against this server; the per-boot server id stops a capture
 * against one server being replayed on another. The host verifies with
 * monocypher's crypto_eddsa_check and later byte-compares pk against pk:
 * entries in the standing list. A bad signature is not a kick: the client is
 * crew, as if it had presented no key.
 *
 * monocypher 4.0.2 is vendored at port/external/monocypher.c and
 * port/include/external/monocypher.h, beside enet.
 */

#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "net/netenet.h"
#include "net/net.h"
#include "types.h"
#include "bss.h"
#include "config.h"
#include "system.h"
#include "external/monocypher.h"

#define CAPKEY_SEED_LEN 32

static char s_keyName[48];                      // "Pak.<serial>.CaptainKey", the bound ini key
static char s_seedHex[CAPKEY_SEED_LEN * 2 + 1]; // the ini-bound value
static u32 s_boundSerial = 0xFFFFFFFFu;         // which pak's key is bound; sentinel = none
static s32 s_haveKey = 0;
static u8 s_secret[64];                         // monocypher's expanded secret key
static u8 s_public[32];

u8 g_NetServerId[32];
static s32 s_serverIdMinted = 0;

u8 g_NetChallengeNonce[32];
u8 g_NetChallengeServerId[32];
s32 g_NetHaveChallenge = 0;

static s32 hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static s32 netCaptainKeyParseSeed(const char *hex, u8 seed[CAPKEY_SEED_LEN])
{
	if (strlen(hex) != CAPKEY_SEED_LEN * 2) {
		return 0;
	}
	for (s32 i = 0; i < CAPKEY_SEED_LEN; i++) {
		const s32 hi = hexval(hex[i * 2]);
		const s32 lo = hexval(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			return 0;
		}
		seed[i] = (u8)((hi << 4) | lo);
	}
	return 1;
}

static void netCaptainKeyDerive(const u8 seed[CAPKEY_SEED_LEN])
{
	u8 tmp[CAPKEY_SEED_LEN];
	memcpy(tmp, seed, sizeof(tmp)); // crypto_eddsa_key_pair wipes its seed argument
	crypto_eddsa_key_pair(s_secret, s_public, tmp);
	s_haveKey = 1;
}

static s32 netCaptainKeyMint(void)
{
	u8 seed[CAPKEY_SEED_LEN];

	if (!netRandomBytes(seed, sizeof(seed))) {
		sysLogPrintf(LOG_ERROR, "NET: captain key: no random source; joining without a key");
		s_haveKey = 0;
		return 0;
	}
	for (s32 i = 0; i < CAPKEY_SEED_LEN; i++) {
		snprintf(s_seedHex + i * 2, 3, "%02x", seed[i]);
	}
	netCaptainKeyDerive(seed);
	crypto_wipe(seed, sizeof(seed));
	return 1;
}

/**
 * Bind the key for the pak the local player's file lives on (slot 0's guid;
 * the no-save sentinel serial 0xffff gets a key too, shared by every seat
 * that has no pak). Loads the seed from pd.ini or mints one. Idempotent;
 * rebinds when the pak changes (--player mid-session).
 */
void netCaptainKeyLoad(void)
{
	const u32 serial = g_PlayerConfigsArray[0].fileguid.deviceserial;
	u8 seed[CAPKEY_SEED_LEN];

	if (serial == s_boundSerial && s_haveKey) {
		return;
	}
	if (s_boundSerial != 0xFFFFFFFFu) {
		configUnbindKey(s_keyName);
	}
	snprintf(s_keyName, sizeof(s_keyName), "Pak.%x.CaptainKey", serial);
	s_seedHex[0] = '\0';
	configRegisterString(s_keyName, s_seedHex, sizeof(s_seedHex)); // max is the buffer size: it copies max-1
	s_boundSerial = serial;

	if (netCaptainKeyParseSeed(s_seedHex, seed)) {
		netCaptainKeyDerive(seed);
		crypto_wipe(seed, sizeof(seed));
	} else {
		if (s_seedHex[0]) {
			sysLogPrintf(LOG_WARNING, "NET: captain key: %s is not a 64-hex seed; minting a new one", s_keyName);
		}
		netCaptainKeyMint();
	}

	if (s_haveKey) {
		char shortpk[12];
		netPkShort(s_public, shortpk);
		sysLogPrintf(LOG_NOTE, "NET: captain key for pak %x: pk=%s", serial, shortpk);
	}
}

/** `captain key new`: rotate the bound pak's key. Returns 1 on success. */
s32 netCaptainKeyNew(void)
{
	netCaptainKeyLoad();
	if (!netCaptainKeyMint()) {
		return 0;
	}
	char shortpk[12];
	netPkShort(s_public, shortpk);
	sysLogPrintf(LOG_NOTE, "NET: captain key rotated for pak %x: pk=%s", s_boundSerial, shortpk);
	return 1;
}

/** Answer a challenge. Returns 1 and fills pk/sig when there is a key. */
s32 netCaptainKeySign(const u8 nonce[32], const u8 serverid[32], u8 pk[32], u8 sig[64])
{
	u8 msg[64];

	netCaptainKeyLoad();
	if (!s_haveKey) {
		return 0;
	}
	memcpy(msg, nonce, 32);
	memcpy(msg + 32, serverid, 32);
	crypto_eddsa_sign(sig, s_secret, msg, sizeof(msg));
	memcpy(pk, s_public, 32);
	// --debug-bad-captain-sig: present the key with a broken signature, so a
	// harness can watch the host refuse it (and still seat us as crew)
	if (sysArgCheck("--debug-bad-captain-sig")) {
		sig[0] ^= 1;
	}
	return 1;
}

/** Host side: did this client prove possession of pk against our challenge? */
s32 netCaptainKeyVerify(const u8 nonce[32], const u8 pk[32], const u8 sig[64])
{
	u8 msg[64];
	memcpy(msg, nonce, 32);
	memcpy(msg + 32, g_NetServerId, 32);
	return crypto_eddsa_check(sig, pk, msg, sizeof(msg)) == 0;
}

/**
 * The per-boot server id: minted on first use, so a capture against one
 * server (or one boot of it) does not replay on another. Per-boot random
 * rather than a hash of name+port is a lean, not her ruling (c-net-two-tiers).
 */
void netServerIdEnsure(void)
{
	if (s_serverIdMinted) {
		return;
	}
	if (!netRandomBytes(g_NetServerId, sizeof(g_NetServerId))) {
		// still unpredictable enough to stop a cross-server replay in practice,
		// but say so
		sysLogPrintf(LOG_WARNING, "NET: no random source for the server id; using the clock");
		const u32 t = enet_time_get();
		memcpy(g_NetServerId, &t, sizeof(t));
	}
	s_serverIdMinted = 1;
}

static const char s_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** Standard base64 of a 32-byte public key: 44 chars + NUL. The pk: principal body. */
void netPkBase64(const u8 pk[32], char out[45])
{
	s32 o = 0;
	for (s32 i = 0; i < 30; i += 3) {
		const u32 v = ((u32)pk[i] << 16) | ((u32)pk[i + 1] << 8) | pk[i + 2];
		out[o++] = s_b64[(v >> 18) & 63];
		out[o++] = s_b64[(v >> 12) & 63];
		out[o++] = s_b64[(v >> 6) & 63];
		out[o++] = s_b64[v & 63];
	}
	// 32 = 10*3 + 2: the tail pair
	const u32 v = ((u32)pk[30] << 16) | ((u32)pk[31] << 8);
	out[o++] = s_b64[(v >> 18) & 63];
	out[o++] = s_b64[(v >> 12) & 63];
	out[o++] = s_b64[(v >> 6) & 63];
	out[o++] = '=';
	out[o] = '\0';
}

/** The first 8 chars of the base64, for logs and the diary. "none" for no key. */
void netPkShort(const u8 *pk, char out[12])
{
	if (!pk) {
		snprintf(out, 12, "none");
		return;
	}
	char full[45];
	netPkBase64(pk, full);
	memcpy(out, full, 8);
	out[8] = '\0';
}
