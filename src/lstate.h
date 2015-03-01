/*
** $Id: lstate.h,v 2.24.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

/* 只要在头文件中包含nlua.h即是内部头文件 */

#ifndef lstate_h
#define lstate_h

#include "nlua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"
#include "lopcodes.h"

struct lua_longjmp;  /* defined in ldo.c */


/* 获取全局哈希表 */
#define gt(L)	(&L->l_gt)

/* 寄存器 */
#define registry(L)	(&G(L)->l_registry)

/* 扩展的栈空间来放置元操作调用以及一些其他的扩展 */
#define EXTRA_STACK   5


#define BASIC_CI_SIZE           8

#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)

/* 字符串表 */
typedef struct stringtable {
  GCObject **hash;    /* hash表 */
  lu_int32 nuse;      /* 正在使用的hash节点 */
  int size;           /* hash表的总大小 */
} stringtable;

/* 调用信息结构 */
typedef struct CallInfo {
  StkId base;                 /* 当前函数的栈基 */
  StkId func;                 /* 函数在栈上的索引 */
  StkId	top;                  /* 当前函数的栈顶 */
  const Instruction *savedpc; /* 用于保存到的pc */
  int nresults;               /* expected number of results from this function */
  int tailcalls;              /* number of tail calls lost under this entry */
} CallInfo;

#define curr_func(L)	(clvalue(L->ci->func))
#define ci_func(ci)   (clvalue((ci)->func))
#define f_isLua(ci)   (!ci_func(ci)->c.isC)
#define isLua(ci)     (ttisfunction((ci)->func) && f_isLua(ci))

/* 指令处理之前统一调用的函数原型 */
typedef int (*nluaV_InstructionStart) (lua_State* L, Instruction *pins);
/* 指令处理返回前调用的函数原型 */
typedef int (*nluaV_InstructionEnd) (lua_State* L, Instruction *pins);
/* 加密指令 */
typedef int (*nluaV_EnInstruction) (lua_State* L, Instruction *pins);
/* 解密指令 */
typedef int (*nluaV_DeInstruction) (lua_State* L, Instruction *pins);
/* 加密指令数据 */
typedef int (*nluaV_EnInstructionData) (lua_State* L, Instruction *pins);
/* 解密指令数据 */
typedef int (*nluaV_DeInstructionData) (lua_State* L, Instruction *pins);
/* 加密缓存 */
typedef int (*nluaV_EnBuffer) (lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize);
/* 解密缓存 */
typedef int (*nluaV_DeBuffer) (lua_State* L, lu_int32 key, lu_byte *p1, lu_byte*p2, int bsize);
/* 制作文件形式的密钥 */
typedef lu_int32 (*nluaV_MakeFileKey) (lua_State* L, const char *path);
/* 指令处理原型 */
typedef int (*nluaV_Instruction) (lua_State* L, Instruction ins, StkId* base, LClosure* cl,
  const Instruction** pc, int* pnexeccalls);

/* 指令编码结构 */
typedef struct OPCODE_RULE {
  OpCode optab[NUM_OPCODES];                  /* opcode编码表 */
  OpRun opmods[NUM_OPCODES];                  /* opcode模式表 */
  nluaV_Instruction opcodedisp[NUM_OPCODES];  /* opcode分派函数 */
  const char* opnames[NUM_OPCODES+1];         /* opcode名称表 */
} OPR;

#define MAX_KEY_PATH              128
/* `全局状态`,所有的线程共享这个状态 */
typedef struct global_State {
  stringtable strt;                 /* 字符串哈希表 */
  lua_Alloc frealloc;               /* 分配内存函数指针 */
  void *ud;                         /* `frealloc'的辅助数据 */
  lu_byte currentwhite;
  lu_byte gcstate;                  /* 垃圾回收状态 */
  int sweepstrgc;                   /* position of sweep in `strt' */
  GCObject *rootgc;                 /* 所有可回收内存对象列表 */
  GCObject **sweepgc;               /* position of sweep in `rootgc' */
  GCObject *gray;                   /* list of gray objects */
  GCObject *grayagain;              /* list of objects to be traversed atomically */
  GCObject *weak;                   /* list of weak tables (to be cleared) */
  GCObject *tmudata;                /* last element of list of userdata to be GC */
  Mbuffer buff;                     /* temporary buffer for string concatentation */
  lu_mem GCthreshold;
  lu_mem totalbytes;                /* 总共分配了多少个字节 */
  lu_mem estimate;                  /* an estimate of number of bytes actually in use */
  lu_mem gcdept;                    /* how much GC is `behind schedule' */
  int gcpause;                      /* size of pause between successive GCs */
  int gcstepmul;                    /* GC `granularity' */
  lua_CFunction panic;              /* 当没有处理的错误发生时，调用此函数 */
  TValue l_registry;
  struct lua_State *mainthread;
  UpVal uvhead;                     /* 一个双向的upvalue值的链表 */
  struct Table *mt[NUM_TAGS];       /* 元操作表 */
  TString *tmname[TM_N];            /* 元操作名称表 */
  
  /* nlua
   */
  int nboot;                        /* 标记已经启动 */
  int is_nlua;                      /* 是nlua的文件格式 */
  OPR oprule;                       /* opcode编码规则 */
  unsigned int nopt;                /* nlua的安全选项 */
  unsigned int ekey;                /* 解密所需的密码 */
  
  //Table* clotab;                    /* 闭包纪录表 */
  
  /* 指令调用前后要执行的函数 */
  nluaV_InstructionStart istart;    /* 指令开始前执行的函数 */
  nluaV_InstructionEnd iend;        /* 指令结束后执行的函数 */
  nluaV_EnInstructionData ienidata; /* 加密指令数据 */
  nluaV_DeInstructionData ideidata; /* 解密指令数据 */
  nluaV_EnInstruction ienins;       /* 加密指令 */
  nluaV_DeInstruction ideins;       /* 解密指令 */
  nluaV_EnBuffer enbuf;             /* 加密缓存 */
  nluaV_DeBuffer debuf;             /* 解密缓存 */
  nluaV_MakeFileKey fkmake;         /* 对文件内容取4字节哈希值 */
  
} global_State;

/* 独立线程状态，每条线程私有*/
struct lua_State {
  CommonHeader;
  lu_byte status;
  StkId top;                          /* 栈顶，在栈上第一个空闲的位置 */
  StkId base;                         /* 当前函数的栈基 */
  global_State *l_G;                  /* 全局状态 */
  CallInfo *ci;                       /* 当前函数的调用信息 */
  const Instruction *savedpc;         /* 当前函数的`savedpc' */
  StkId stack_last;                   /* 栈末尾，在栈上最后空闲的位置 */
  StkId stack;                        /* 栈的起始 */
  CallInfo *end_ci;                   /* 指向CallInfo队列的末尾指针 */
  CallInfo *base_ci;                  /* CallInfo队列 */
  int stacksize;
  int size_ci;                        /* `base_ci'队列的长度 */
  unsigned short nCcalls;             /* number of nested C calls */
  unsigned short baseCcalls;          /* nested C calls when resuming coroutine */
  lu_byte hookmask;
  lu_byte allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  TValue l_gt;                        /* 全局变量表 */
  TValue env;                         /* temporary place for environments */
  GCObject *openupval;                /* list of open upvalues in this stack */
  GCObject *gclist;
  struct lua_longjmp *errorJmp;       /* 当前错误恢复点 */
  ptrdiff_t errfunc;                  /* 当前的错误处理句柄 (栈索引) */
};

/* 从本地线程状态返回全局状态 */
#define G(L)	(L->l_G)

/* 所有可回收内存对象的联合体
 */
union GCObject {
  GCheader gch;
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;  /* thread */
};

/* 一些宏操作:转换 GCObject 为指定的值 */
#define rawgco2ts(o)	check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2cl(o)	check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o)	check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)	check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)	check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define ngcotouv(o) \
	check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)	check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

/* 转换任何Lua对象到一个GCObject */
#define obj2gco(v)	(cast(GCObject *, (v)))


LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);

/*
 * nlua
 */
LUAI_FUNC void nluaE_setopt (lua_State *L, unsigned int opt);
LUAI_FUNC void nluaE_setnlua (lua_State *L, int is_nlua);
LUAI_FUNC void nluaE_setkey (lua_State *L, unsigned int key);

#endif

