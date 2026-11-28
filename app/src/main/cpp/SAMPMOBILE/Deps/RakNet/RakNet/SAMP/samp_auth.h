// https://github.com/P3ti/RakSAMP/blob/master/raknet/SAMP/samp_auth.h

extern char AuthKeyTable[512][2][128];
void gen_auth_key(char buf[260], const char* auth_in);
