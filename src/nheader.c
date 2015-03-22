//
//  nheader.c
//  nlua
//
//  Created by logic.yan on 15/3/22.
//  Copyright (c) 2015年 naga. All rights reserved.
//

#include <stdio.h>
#include <string.h>

#include "nheader.h"
#include "lundump.h"
#include "nundump.h"

void nluaU_makehdr(nheader *h, unsigned int magic,
                  unsigned char version, unsigned char format) {
  int x=1;
  
  h->magic=magic;
  h->version=version;
  h->format=format;
  h->byteord=(unsigned char)*(char*)&x;
  h->ints=(unsigned char)sizeof(int);
  h->sizes=(unsigned char)sizeof(size_t);
  h->inss=(unsigned char)sizeof(Instruction);
  h->nums=(unsigned char)sizeof(lua_Number);
  h->number_is_integral=(unsigned char)(((lua_Number)0.5)==0);
}

int nluaU_hdrisvalid(nheader *h) {
  if (memcmp(&(h->magic),NLUA_SIGNATURE,4) == 0) {
    if ((h->version == NLUAC_VERSION) && (h->format == NLUAC_FORMAT)) {
      return 1;
    }
  } else if (memcmp(&(h->magic),LUA_SIGNATURE,4) == 0) {
    if ((h->version == LUAC_VERSION) && (h->format == LUAC_FORMAT)) {
      return 1;
    }
  }
  
  return 0;
}

