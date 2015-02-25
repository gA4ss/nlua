/*
** $Id: luac.c,v 1.54 2006/06/02 17:37:11 lhf Exp $
** Lua compiler (saves bytecodes to files; also list bytecodes)
** See Copyright Notice in lua.h
*/

#define nlua_compile
#ifdef nlua_compile

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define nluac_c
#define LUA_CORE

#include "lua.h"
#include "lauxlib.h"

#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lundump.h"

/*
 * nlua
 */
#include "nundump.h"
#include "crc.h"
#include "xor.h"

#define PROGNAME	"nluac"               /* default program name */
#define	OUTPUT		PROGNAME ".out"       /* default output file */

static int usenlua=0;                   /* 使用nlua */
static int listing=0;                   /* 列出字节码? */
static int dumping=1;                   /* 编译字节码? */
static int stripping=0;                 /* 剔除调试信息? */
static int rop=0;                       /* 随机字节码? */
static int eid=0;                       /* 加密指令数据部分? */
static int ei=0;                        /* 加密整体指令? */
static int ef=0;                        /* 加密整体文件? */
static int ed=0;                        /* 加密数据 */
static int efk=0;                       /* 加密整体文件使用文件hash作为key? */
static unsigned int fkey=0;             /* 加密整体文件使用key? */
static unsigned int fks=0;              /* 加密整体文件使用的key长度*/
static char fkeyp[128]={ 0 };           /* 相对文件路径 */
static char Output[]={ OUTPUT };        /* 默认输出文件名 */
static const char* output=Output;       /* actual output file name */
static const char* progname=PROGNAME;   /* actual program name */

static void fatal(const char* message) {
  fprintf(stderr,"%s: %s\n",progname,message);
  exit(EXIT_FAILURE);
}

static void cannot(const char* what) {
  fprintf(stderr,"%s: cannot %s %s: %s\n",progname,what,output,strerror(errno));
  exit(EXIT_FAILURE);
}

static void usage(const char* message) {
  if (*message=='-')
    fprintf(stderr,"%s: unrecognized option " LUA_QS "\n",progname,message);
  else
    fprintf(stderr,"%s: %s\n",progname,message);
  fprintf(stderr,
          "usage: %s [options] [filenames].\n"
          "Available options are:\n"
          "  -        process stdin\n"
          "  -l       list\n"
          "  -o name  output to file " LUA_QL("name") " (default is \"%s\")\n"
          "  -p       parse only\n"
          "  -s       strip debug information\n"
          "  -v       show version information\n"
          "  --       stop handling options\n"
          "Naga Lua options:\n"
          "  -1       random opcode\n"
          "  -2       encrypt data of instruction\n"
          "  -3       encrypt instruction\n"
          "  -4       encrypt bytecode file\n"
          "  -5       encrypt data\n"
          "  -k key   select \"-4\" or \"-5\" key\n"
          "  -x path  \"-k\"\'s key is file path\n",
          progname,Output);
  exit(EXIT_FAILURE);
}

#define	IS(s)	(strcmp(argv[i],s)==0)

/* 产生文件key */
static unsigned int makefilekey(char* filename) {
  FILE* fp=NULL;
  unsigned int fkey=0;
  long fsize=0;
  size_t r=0;
  void* fbuf=NULL;
  
  fp = fopen(filename, "rb");
  if (fp == NULL) {
    cannot("open file");
  }
  
  /* 获取文件长度 */
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  /* 读取文件 */
  fbuf=malloc(fsize);
  if (fbuf == NULL) {
    cannot("malloc");
  }
  r = fread(fbuf, 1, fsize, fp);
  if (r != fsize) {
    cannot("malloc");
  }
  
  fkey=(unsigned int)crc32((unsigned char*)fbuf, (unsigned int)fsize);
  free(fbuf);
  fclose(fp);
  
  return fkey;
}

/* 分析命令行 */
static int doargs(int argc, char* argv[]) {
  int i;
  int version=0;
  memset(fkeyp, 0, 128);    /* 0值为了做判断 */
  if (argv[0]!=NULL && *argv[0]!=0) progname=argv[0];
  for (i=1; i<argc; i++) {
    if (*argv[i]!='-')			/* 命令行处理结束 */
      break;
    else if (IS("--")) {    /* 命令行结束 跳过它 */
      ++i;
      if (version) ++version;
      break;
    } else if (IS("-"))			/* 命令行结束,使用stdin */
      break;
    else if (IS("-l"))			/* list */
      ++listing;
    else if (IS("-o")) {    /* 输出文件 */
      output=argv[++i];
      if (output==NULL || *output==0) usage(LUA_QL("-o") " needs argument");
      if (IS("-")) output=NULL;
    }
    else if (IS("-p"))			/* 仅作语法分析 */
      dumping=0;
    else if (IS("-s"))			/* 剔除调试信息 */
      stripping=1;
    else if (IS("-v"))			/* 显示版本 */
      ++version;
    else if (IS("-1")) {	  /* 随机字节码 */
      ++rop;
      ++usenlua;
    } else if (IS("-2")) {  /* 加密指令数据 */
      ++eid;
      ++usenlua;
    } else if (IS("-3")) {	/* 加密指令 */
      ++ei;
      ++usenlua;
    } else if (IS("-4")) {  /* 加密字节文件 */
      ++ef;
      ++usenlua;
    } else if (IS("-5")) {  /* 加密数据 */
      ++ed;
      ++usenlua;
    } else if (IS("-k")) {	/* 加密字节文件的密码 */
      char* kstr=argv[++i];
      if (kstr==NULL || *kstr==0) usage(LUA_QL("-k") " needs argument");
      
      /* 计算密码长度 */
      if (efk==0) {
        fkey=crc32((unsigned char*)kstr, (unsigned int)strlen(kstr));
        fks=4;
        /* 纪录下,为了-x参数在-k参数之后的情况做准备 */
        strcpy(fkeyp, kstr);
      } else {
        fkey=makefilekey(kstr);
        /* fks在-x中进行赋值 */
      }
    } else if (IS("-x")) {	/* 加密字节文件使用文件的hash值 */
      char* fk = argv[++i];
      if (fk==NULL || *fk==0) usage(LUA_QL("-x") " needs argument");
      /* 如果 -k 选项在前面就会造成这样的结果 */
      if (fkeyp[0] != 0) {
        fkey=makefilekey(fkeyp);
      }
      strcpy(fkeyp, fk);
      fks=(unsigned int)strlen(fkeyp);
      ++efk;
    } else                  /* 未知选项 */
      usage(argv[i]);
  }
  if (i==argc && (listing || !dumping)) {
    dumping=0;
    argv[--i]=Output;
  }
  
  if (ef || ed) {
    if (fkey==0) {
      fkey=0x19830613;
      fks=4;
    }
  }
  
  /* 打印版本 */
  if (version) {
    printf("%s  %s\n",NLUA_RELEASE,NLUA_COPYRIGHT);
    if (version==argc-1) exit(EXIT_SUCCESS);
  }
  
  if (usenlua) {
    printf("use %d \"nLua\" protect function\n", usenlua);
    printf("please view <http://www.nagain.com> to get more security tools\n");
  }
  
  return i;
}

#define toproto(L,i) (clvalue(L->top+(i))->l.p)

static const Proto* combine(lua_State* L, int n) {
  /* 如果仅有一个源代码文件则直接返回 */
  if (n==1)
    return toproto(L,-1);
  else {
    /* 合成一个入口函数,这个函数包含了多份文件中的入口函数 */
    int i,pc;
    Proto* f=luaF_newproto(L);
    setptvalue2s(L,L->top,f); incr_top(L);
    f->source=luaS_newliteral(L,"=(" PROGNAME ")");
    f->maxstacksize=1;
    pc=2*n+1;
    f->code=luaM_newvector(L,pc,Instruction);
    f->sizecode=pc;
    f->p=luaM_newvector(L,n,Proto*);
    f->sizep=n;
    pc=0;
    
    /* 遍历这些子函数,并且汇编调用代码 */
    for (i=0; i<n; i++) {
      f->p[i]=toproto(L,i-n-1);
      f->code[pc++]=CREATE_ABx(OP_CLOSURE,0,i);
      f->code[pc++]=CREATE_ABC(OP_CALL,0,1,1);
    }
    /* 汇编一条返回指令 */
    f->code[pc++]=CREATE_ABC(OP_RETURN,0,1,0);
    return f;
  }
}

static int writer(lua_State* L, const void* p, size_t size, void* u) {
  UNUSED(L);
  return (fwrite(p,size,1,(FILE*)u)!=1) && (size!=0);
}

struct Smain {
  int argc;
  char** argv;
};

static int pmain(lua_State* L) {
  struct Smain* s = (struct Smain*)lua_touserdata(L, 1);
  int argc=s->argc;
  char** argv=s->argv;
  const Proto* f;
  int i;
  if (!lua_checkstack(L,argc)) fatal("too many input files");
  
  /* 可以一次加载多个lua文件 */
  for (i=0; i<argc; i++) {
    const char* filename=IS("-") ? NULL : argv[i];
    if (luaL_loadfile(L,filename)!=0) fatal(lua_tostring(L,-1));
  }
  /* 将这些代码文件组合到一起 */
  f=combine(L,argc);
  
  /* 反汇编 */
  if (listing) luaU_print(f,listing>1);
  
  /* 输出为字节代码文件 */
  if (dumping) {
    unsigned char* nopt;
    long nopts;
    FILE* D= (output==NULL) ? stdout : fopen(output,"wb");
    if (D==NULL) cannot("open");
    lua_lock(L);
    if (efk)
      nopts=sizeof(NagaLuaOpt)+strlen(fkeyp);
    else
      nopts=sizeof(NagaLuaOpt)+4;
    
    nopt=(unsigned char*)malloc(nopts);
    if (nopt==NULL) cannot("malloc");
    memset(nopt, 0, nopts);
    /* 填充选项 */
    if (rop) {
      nlo_set_rop(nopt);
    }
    
    if (eid) {
      nlo_set_eid(nopt);
    }
    
    if (ei) {
      nlo_set_ei(nopt);
    }
    
    if (ef || ed) {
      
      if (ef) {
        nlo_set_ef(nopt);
      }
      
      if (ed) {
        nlo_set_ed(nopt);
      }
      
      if (efk) {
        /* 使用文件key */
        nlo_set_efk(nopt);
        memcpy(nopt+sizeof(NagaLuaOpt), fkeyp, fks);
      } else {
        memcpy(nopt+sizeof(NagaLuaOpt), &fkey, fks);
      }
      
      /* 设置密码长度 */
      nlo_set_ks(nopt,fks);
    }
    
    nluaU_dump(L,f,writer,D,stripping,(NagaLuaOpt*)nopt, fkey);
    lua_unlock(L);
    if (ferror(D)) cannot("write");
    if (fclose(D)) cannot("close");
  }
  return 0;
}

int main(int argc, char* argv[]) {
  lua_State* L;
  struct Smain s;
  int i=doargs(argc,argv);
  argc-=i; argv+=i;
  if (argc<=0) usage("no input files given");
  L=lua_open();
  if (L==NULL) fatal("not enough memory for state");
  s.argc=argc;
  s.argv=argv;
  if (lua_cpcall(L,pmain,&s)!=0) fatal(lua_tostring(L,-1));
  lua_close(L);
  return EXIT_SUCCESS;
}

#endif
