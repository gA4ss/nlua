#if !defined(__XOR_H__)
#define __XOR_H__

#if defined(__cplusplus)
extern "C"
{
#endif

unsigned int PolyXorKey(unsigned int dwKey);
void XorArray(unsigned int dwKey, unsigned char* pPoint, unsigned char* pOut, unsigned int iLength);
void XorCoder(unsigned char* pKey, unsigned char* pBuffer, unsigned int iLength);
void XorKey32Bits(unsigned int dwKeyContext, unsigned char* pKey, unsigned int iKeyLength);

#if defined(__cplusplus)
}
#endif

#endif