#pragma once
#include <wincrypt.h>
static BOOL shader_digest_matches(const BYTE *code,DWORD n,const BYTE approved[32])
{
    HCRYPTPROV prov=0; HCRYPTHASH hash=0;
    BYTE sum[32]; DWORD size=sizeof sum;
    BOOL ok=CryptAcquireContextW(&prov,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(prov,CALG_SHA_256,0,0,&hash) && CryptHashData(hash,code,n,0) &&
        CryptGetHashParam(hash,HP_HASHVAL,sum,&size,0) && size==sizeof sum && !memcmp(sum,approved,sizeof sum);
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov,0);
    return ok;
}
