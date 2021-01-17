#ifndef SHA256CRYPT_H
#define SHA256CRYPT_H

extern "C" {

char *
sha256_crypt (const char *key, const char *salt);

}

#endif // SHA256CRYPT_H
