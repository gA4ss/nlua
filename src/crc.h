#if !defined(__CRC_H__)
#define __CRC_H__

#if defined(__cplusplus)
extern "C"
{
#endif

#define UPDC32(octet, crc)\
  (unsigned int)((crc_32_tab[(((unsigned int)(crc)) ^ ((unsigned char)(octet))) & 0xff] ^ (((unsigned int)(crc)) >> 8)))

unsigned int naga_crc32(unsigned char* data, unsigned int length);
unsigned int naga_crc32int(unsigned int *data);
unsigned char naga_crc32_selftests ();
	
extern unsigned int crc_32_tab[];
	
#if defined(__cplusplus)
}
#endif

#endif
