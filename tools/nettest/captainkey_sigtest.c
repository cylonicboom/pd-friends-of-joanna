// the captain-key handshake, standalone, against the vendored monocypher:
// sig = ed25519(seed, nonce || serverid) as netcaptainkey.c does it.
//
//   cc -std=c99 -Wall -Wextra -I port/include port/external/monocypher.c tools/nettest/captainkey_sigtest.c -o /tmp/sigtest && /tmp/sigtest
//
// PASS means: a good signature verifies; the same signature against another
// server id fails (no cross-server replay); against another nonce fails (no
// same-server replay); a flipped bit fails; a wrong public key fails; and
// the same seed always gives the same public key, so the stored seed is the
// whole identity.
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "external/monocypher.h"
static int rnd(uint8_t *o, int n){int fd=open("/dev/urandom",O_RDONLY); int g=0; while(g<n){int r=read(fd,o+g,n-g); if(r<=0)break; g+=r;} close(fd); return g==n;}
static void msg(uint8_t out[64], const uint8_t nonce[32], const uint8_t sid[32]){memcpy(out,nonce,32);memcpy(out+32,sid,32);}
int main(void){
  uint8_t seed[32], sk[64], pk[32], nonce[32], sid[32], sid2[32], m[64], sig[64];
  rnd(seed,32); crypto_eddsa_key_pair(sk, pk, seed); // seed is wiped by this call in 4.x
  rnd(nonce,32); rnd(sid,32); rnd(sid2,32);
  msg(m,nonce,sid); crypto_eddsa_sign(sig, sk, m, 64);
  int fails=0;
  printf("good sig verifies      -> %d (want 0)\n", crypto_eddsa_check(sig, pk, m, 64)); fails += crypto_eddsa_check(sig,pk,m,64)!=0;
  msg(m,nonce,sid2); printf("other server id        -> %d (want -1)\n", crypto_eddsa_check(sig, pk, m, 64)); fails += crypto_eddsa_check(sig,pk,m,64)==0;
  uint8_t nonce2[32]; rnd(nonce2,32); msg(m,nonce2,sid); printf("other nonce (replay)   -> %d (want -1)\n", crypto_eddsa_check(sig, pk, m, 64)); fails += crypto_eddsa_check(sig,pk,m,64)==0;
  msg(m,nonce,sid); sig[3]^=1; printf("bit-flipped sig        -> %d (want -1)\n", crypto_eddsa_check(sig, pk, m, 64)); fails += crypto_eddsa_check(sig,pk,m,64)==0; sig[3]^=1;
  uint8_t pk2[32]; memcpy(pk2,pk,32); pk2[0]^=1; printf("wrong public key       -> %d (want -1)\n", crypto_eddsa_check(sig, pk2, m, 64)); fails += crypto_eddsa_check(sig,pk2,m,64)==0;
  // determinism: same seed -> same pk (so the stored seed is the whole identity)
  uint8_t seedA[32]={1,2,3}, seedB[32]={1,2,3}, skA[64], pkA[32], skB[64], pkB[32];
  crypto_eddsa_key_pair(skA,pkA,seedA); crypto_eddsa_key_pair(skB,pkB,seedB);
  printf("same seed same pk      -> %d (want 1)\n", memcmp(pkA,pkB,32)==0); fails += memcmp(pkA,pkB,32)!=0;
  printf("%s\n", fails?"FAIL":"PASS"); return fails; }
