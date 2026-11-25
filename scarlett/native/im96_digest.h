#pragma once
#include <bcrypt.h>
static BOOL im96_digest_matches(const BYTE *code,DWORD n,const BYTE approved[32])
{
    BYTE sum[32];
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)code,n,sum,sizeof sum)==0 &&
        !memcmp(sum,approved,sizeof sum);
}
