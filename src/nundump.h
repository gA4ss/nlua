#ifndef nundump_h
#define nundump_h

#include "lobject.h"
#include "lzio.h"
#include "crc.h"
#include "xor.h"

/* Naga Lua操作选项 */
typedef struct naga_lua_options {
    /* 低4位控制
     * 0:OpCode随机化
     * 1:指令数据加密
     * 2:指令整体加密
     * 3:文件整体加密
     * 4:文件整体加密的密钥使用某个文件的hash值
     * 5:加密数据
     */
    unsigned char opt;
    unsigned int ks;            /* 文件整体解密所需的key长度 */
    /* 这后面紧跟解密文件所需的密钥 */
} NagaLuaOpt;

#define sizeofnlo(o)  (sizeof(NagaLuaOpt) + ((NagaLuaOpt*)o)->ks)

#define nlo_rop(o)    (((NagaLuaOpt*)o)->opt & 0x01)
#define nlo_eid(o)    (((NagaLuaOpt*)o)->opt & 0x02)
#define nlo_ei(o)     (((NagaLuaOpt*)o)->opt & 0x04)
#define nlo_ef(o)     (((NagaLuaOpt*)o)->opt & 0x08)
#define nlo_efk(o)    (((NagaLuaOpt*)o)->opt & 0x10)
#define nlo_ed(o)     (((NagaLuaOpt*)o)->opt & 0x20)
#define nlo_ks(o)     (((NagaLuaOpt*)o)->ks)

#define nlo_opt_rop(o)    ((o) & 0x01)
#define nlo_opt_eid(o)    ((o) & 0x02)
#define nlo_opt_ei(o)     ((o) & 0x04)
#define nlo_opt_ef(o)     ((o) & 0x08)
#define nlo_opt_efk(o)    ((o) & 0x10)
#define nlo_opt_ed(o)     ((o) & 0x20)

#define nlo_set_rop(o)    (((NagaLuaOpt*)o)->opt |= 0x01)
#define nlo_set_eid(o)    (((NagaLuaOpt*)o)->opt |= 0x02)
#define nlo_set_ei(o)     (((NagaLuaOpt*)o)->opt |= 0x04)
#define nlo_set_ef(o)     (((NagaLuaOpt*)o)->opt |= 0x08)
#define nlo_set_efk(o)    (((NagaLuaOpt*)o)->opt |= 0x10)
#define nlo_set_ed(o)     (((NagaLuaOpt*)o)->opt |= 0x20)
#define nlo_set_ks(o,k)   (((NagaLuaOpt*)o)->ks=(k))

#define nlo_get_key(o)    ((unsigned char*)(o) + sizeof(NagaLuaOpt))

/* load one chunk; from nundump.c */
LUAI_FUNC Proto* nluaU_undump (lua_State* L, ZIO* Z, Mbuffer* buff, const char* name);

/* make header; from nundump.c */
LUAI_FUNC void nluaU_header (char* h);

/* 产生文件key; from nundump.c */
LUAI_FUNC unsigned int nluaU_makefilekey(lua_State *L, char* filename);

/* dump one chunk; from ndump.c */
LUAI_FUNC int nluaU_dump (lua_State* L, const Proto* f, lua_Writer w,
                          void* data, int strip, NagaLuaOpt* nopt,
                          unsigned int ekey);

#ifdef nluac_c
/* print one chunk; from print.c */
LUAI_FUNC void luaU_print (lua_State* L, const Proto* f, int full);
#endif

/* for header of binary files -- this is Naga Lua 1.0 */
#define NLUAC_VERSION           0x10

/* for header of binary files -- this is the official format */
#define NLUAC_FORMAT            0

/* size of header of binary files */
#define NLUAC_HEADERSIZE		12

#endif
