#include "xor.h"
#include "crc.h"

unsigned int PolyXorKey(unsigned int dwKey) {
	unsigned int i = 0;
	unsigned char* pKey = (unsigned char*)&dwKey;
	unsigned char bVal = 0, bTmp = 0, bTmp2 = 0;
	dwKey ^= 0x5DEECE66DL + 2531011;
	for (i = 0; i < sizeof(unsigned int); i++, pKey++) {
		unsigned int j = 0, n = 0;
		bVal = *pKey;
		for (j = 0x80, n = 7; j > 0x01; j /= 2, n--) {
			bTmp = (bVal & j) >> n;
			bTmp2 = (bVal & j / 2) >> (n - 1);
			bTmp ^= bTmp2;
			bTmp <<= n;
			bVal |= bTmp;
		}
		bTmp = bVal & 0x01;
		bTmp2 = bVal & 0x80 >> 7;
		bTmp ^= bTmp2;

		*pKey = bVal;
	}/* end for */
	return dwKey;
}

void XorArray(unsigned int dwKey, unsigned char* pPoint, unsigned char* pOut, unsigned int iLength) {
#if 0
	unsigned int dwNextKey = dwKey;
	unsigned char* pKey = (unsigned char*)&dwNextKey;
	unsigned int i = 0, j = 0;
	for (i = 0; i < iLength; i++) {
		pOut[i] = pPoint[i] ^ pKey[j];
		if (j == 3) {
			dwNextKey = PolyXorKey(dwNextKey);
			j = 0;
		} else j++;
	}
#endif
  
  unsigned int i = 0;
  unsigned int key = 0x99999999;
  unsigned int count = iLength / 4;
  unsigned int recount = 0;
  unsigned int *pIntOut = (unsigned int *)pOut;
  unsigned int *pIntPoint = (unsigned int *)pPoint;
  
  for (i = 0; i < count; i++) {
    pIntOut[i] = pIntPoint[i] ^ key;
  }

  recount = iLength % 4;
  for (i = 0; i < recount; i++) {
    pOut[i + count * 4] = pPoint[i + count * 4] ^ 0x99;
  }
}

void XorCoder(unsigned char* pKey, unsigned char* pBuffer, unsigned int iLength) {
	unsigned int i = 0;
	for (i = 0; i < iLength; i++)
		pBuffer[i] = pBuffer[i] ^ pKey[i];
}

void XorKey32Bits(unsigned int dwKeyContext, unsigned char* pKey, unsigned int iKeyLength) {
	unsigned iCount = 0;
	unsigned int dwKey = dwKeyContext;
	unsigned char* pOutPut = pKey;
	iCount = (iKeyLength % sizeof(unsigned int) != 0) ? iKeyLength / sizeof(unsigned int) + 1 : iKeyLength / sizeof(unsigned int);

	while (iCount--) {
		dwKey = PolyXorKey(dwKey);
		*(unsigned int *)pOutPut ^= dwKey;
		pOutPut += sizeof(unsigned int);
	}
}

