//
//  nheader.h
//  nlua
//
//  Created by logic.yan on 15/3/22.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#ifndef nlua_nheader_h
#define nlua_nheader_h

#include "nlua.h"

/*
 * 字节代码头
 */
typedef struct {
  unsigned int magic;
  unsigned char version;
  unsigned char format;
  unsigned char byteord;
  unsigned char ints;
  unsigned char sizes;
  unsigned char inss;
  unsigned char nums;
  unsigned char number_is_integral;
} nheader;

/*
 * NLUA标示
 */
#define NLUAC_VERSION           0x10
#define NLUAC_FORMAT            0
#define NLUAC_HEADERSIZE        12

/*
 * LUA标示
 */
#define LUAC_VERSION            0x51
#define LUAC_FORMAT             0
#define LUAC_HEADERSIZE         12

LUAI_FUNC void nluaU_makehdr(nheader *h, unsigned int magic,
                             unsigned char version, unsigned char format);

LUAI_FUNC int nluaU_hdrisvalid(nheader *h);

#endif
