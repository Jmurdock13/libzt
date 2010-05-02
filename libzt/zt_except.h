/*!
 * Filename: zt_except.h
 * Description: Exception handler for C
 *
 * Author: Jason L. Shiffer <jshiffer@zerotao.org>
 * Copyright:
 *        Copyright (C) 2002-2010, Jason L. Shiffer.
 *        See file COPYING for details
 * 
 * Notes:
 *
 */
#ifndef _ZT_EXCEPT_H_
#define _ZT_EXCEPT_H_

#include <libzt/zt.h>
#include <libzt/zt_macros.h>

#include <setjmp.h>
#include <stdlib.h>


BEGIN_C_DECLS

/* declare exception */
#define _EXCEPT_DECL(NAME, STR)         \
    char * NAME ;

/* define exception */
#define _EXCEPT_DEFN(NAME, STR)         \
    STR,

/* exception */
#define EXCEPTION(NAME, STR)            \
    (MACRO_APPLY, _EXCEPT_DECL, _EXCEPT_DEFN, (NAME, STR))


/* GROUP */
#define _EXCEPT_GROUP_DECL_INIT(STR)            \
    struct {                  \
        char    * desc;

#define _EXCEPT_GROUP_DECL_CLOSE(NAME)          \
    } NAME ;

#define _EXCEPT_GROUP_DEFN_INIT(STR)            \
{ STR,

#define _EXCEPT_GROUP_DEFN_CLOSE(NAME)          \
},

#define EXCEPT_GROUP(NAME, STR, REST)                                   \
    (MACRO_APPLY, _EXCEPT_GROUP_DECL_INIT, _EXCEPT_GROUP_DEFN_INIT, (STR)) \
REST                                                                \
(MACRO_APPLY, _EXCEPT_GROUP_DECL_CLOSE, _EXCEPT_GROUP_DEFN_CLOSE, (NAME))


/* TOP GROUP */
#define _EXCEPT_TYPE(NAME) exceptDomain_ ## NAME

#define _EXCEPT_TOP_GROUP_DECL_INIT(NAME, STR)      \
    typedef struct _EXCEPT_TYPE(NAME) _EXCEPT_TYPE(NAME);   \
    struct _EXCEPT_TYPE(NAME) {                  \
    char    * desc;


#define _EXCEPT_TOP_GROUP_DECL_CLOSE(NAME)      \
};                       \
_EXTERN_DECL(NAME)


#define _EXCEPT_TOP_GROUP_DEFN_INIT(NAME, STR)      \
    _EXCEPT_TYPE(NAME) NAME = { STR ,

#define _EXCEPT_TOP_GROUP_DEFN_CLOSE(NAME)      \
    }

#define _EXCEPT_FORMS(NAME, STR, REST)                                  \
    (MACRO_APPLY, _EXCEPT_TOP_GROUP_DECL_INIT, _EXCEPT_TOP_GROUP_DEFN_INIT, (NAME, STR)) \
REST                                                                \
(MACRO_APPLY_STOP, _EXCEPT_TOP_GROUP_DECL_CLOSE, _EXCEPT_TOP_GROUP_DEFN_CLOSE, (NAME))

/* actual definition */
#define EXCEPT_DECL(NAME, STR, REST) MACRO_EVAL_1(_EXCEPT_FORMS(NAME, STR, REST))
#define EXCEPT_DEFN(NAME, STR, REST) MACRO_EVAL_2(_EXCEPT_FORMS(NAME, STR, REST))

#undef EXCEPT_DESC
#ifdef EXCEPT_DEFINE
# define _EXTERN_DECL(NAME)
# define EXCEPT_DESC(NAME, STR, REST)           \
    EXCEPT_DECL(NAME, STR, REST)                \
EXCEPT_DEFN(NAME, STR, REST)
#else
# define _EXTERN_DECL(NAME) extern _EXCEPT_TYPE(NAME) NAME
# define EXCEPT_DESC(NAME, STR, REST)           \
    EXCEPT_DECL(NAME, STR, REST)
#endif  /* EXCEPT_DEFINE */
#undef EXCEPT_DEFINE

struct except_Frame {
    struct except_Frame * prev;
    int                   phase;
    int                   caught;
    jmp_buf               env;
    void                * exception;
    char                * etext;
    char                * efunc;
    char                * efile;
    int                   eline;
};

typedef int (*except_handler)(void *, char *, char *, char *, int);

enum { except_WindPhase = 0, except_UnWindPhase };
extern char * except_CatchAll;

/* exported variables */
extern struct except_Frame *_except_Stack;

/* exported functions */
extern void except_unhandled_exception(struct except_Frame *stack, int flags);
extern void _except_call_handlers(struct except_Frame *);
extern void _except_unhandled_exception(void *exc, char *etext, const char *efile, unsigned int eline, const char *efunc, int flags);
extern int domain_default_except_handler(void *exc, char *etext, char *file, char *func, int line);

extern except_handler _except_install_default_handler(except_handler h);
#define INSTALL_DEFAULT_HANDLER(HANDLER)    \
    _except_install_default_handler(HANDLER)

extern void _except_install_handler(void*, except_handler h);
#define INSTALL_EXCEPT_HANDLER(EXCEPTION, HANDLER)          \
    _except_install_handler(&EXCEPTION, HANDLER)

extern void _except_remove_handler(void*, except_handler);
#define REMOVE_EXCEPT_HANDLER(EXCEPTION, HANDLER)           \
    _except_remove_handler(&EXCEPTION, HANDLER)

#define RETHROW()                           \
    _except_Stack->caught = 0

#define THROW_TYPED(EXCEPTION, TYPE)                                    \
    do {                                                                \
        if(!_except_Stack) {                                            \
            _except_unhandled_exception(NULL,                           \
                                        #TYPE,                          \
                                        __FILE__,                       \
                                        __LINE__,                       \
                                        __FUNCTION__,                   \
                                        1);                             \
        }                                                               \
        _except_Stack->etext = #TYPE;                                   \
        _except_Stack->efile = (char *)__FILE__;                        \
        _except_Stack->efunc = (char *)__FUNCTION__;                    \
        _except_Stack->eline = __LINE__;                                \
        _except_Stack->caught = 0;                                      \
        _except_Stack->exception = &(EXCEPTION);                        \
        if(_except_Stack->phase == except_WindPhase){                   \
            longjmp(_except_Stack->env, 1);                             \
        }                                                               \
    } while(0)

#define TRY_THROW(EXCEPTION)                        \
    TRY(THROW(EXCEPTION), {})

#define THROW(EXCEPTION)                        \
    THROW_TYPED(EXCEPTION, EXCEPTION);

#define EXCEPTION_IN(DOMAIN)                                            \
    (((_except_Stack->exception == (void *)(&DOMAIN)) ||                 \
      ((_except_Stack->exception >= (void *)(&DOMAIN)) &&               \
       (_except_Stack->exception <= (void *)((intptr_t)&DOMAIN + sizeof(DOMAIN))))) ? 1 : 0)

#define CATCH(EXCEPTION, DATA)                                          \
    if(_except_Stack->phase == except_UnWindPhase){                     \
        void *e = _except_Stack->exception;                             \
        void *e2 = (void *)(&EXCEPTION);                                \
        if((e2 == (void *)&except_CatchAll) ||                          \
           (e2 == e) ||                                                 \
           (e >= e2 && e <= (void *)((void *)(&EXCEPTION) + sizeof(EXCEPTION)))){ \
            _except_Stack->caught = 1;                                  \
            { DATA };                                                   \
        }                                                               \
    }

#define _COPY_FRAME_DATA(x,y)               \
    (y)->exception = (x)->exception;        \
(y)->etext = (x)->etext;                \
(y)->efile = (x)->efile;                \
(y)->efunc = (x)->efunc;                \
(y)->eline = (x)->eline

#define _PUSH_FRAME_DATA(x)             \
    _COPY_FRAME_DATA(x, x->prev)

#define DO_TRY                                              \
    do{                                                     \
        struct except_Frame Frame;                          \
        Frame.prev = _except_Stack;                         \
        Frame.phase = except_WindPhase;                     \
        _except_Stack = &Frame;                             \
        switch(setjmp(_except_Stack->env)){                 \
            case 0:
#define ELSE_TRY                                            \
                break;                                                  \
            case 1:                                                 \
                _except_Stack->phase = except_UnWindPhase;

#define END_TRY                                             \
            default:                            \
                if((_except_Stack->exception) &&                \
                   (_except_Stack->caught == 0)){               \
                    if(_except_Stack->prev != NULL){            \
                        _PUSH_FRAME_DATA(_except_Stack);        \
                        _except_Stack = _except_Stack->prev;    \
                        _except_Stack->caught = 0;              \
                        longjmp(_except_Stack->env, 1);         \
                    }else{                                      \
                        _except_call_handlers(_except_Stack);   \
                    }                                           \
                }                                               \
        }/*unwind end*/                                     \
                _except_Stack = _except_Stack->prev;                \
    }while(0);


#define TRY(WIND_DATA, ...)                                 \
    DO_TRY                                                  \
{ WIND_DATA; }    \
ELSE_TRY                                                \
{ __VA_ARGS__; }    \
END_TRY

#define TRY_RETURN                              \
    _except_Stack = _except_Stack->prev;        \
return

#define UNWIND_PROTECT(WIND_DATA, UNWIND_DATA)                          \
    do {                                                                \
        TRY({                                                           \
                { WIND_DATA; }                                          \
                { UNWIND_DATA;}                                         \
            },                                                          \
            {                                                           \
                CATCH (except_CatchAll, { UNWIND_DATA;  RETHROW(); });  \
            });                                                         \
    }while(0)

END_C_DECLS
#endif /* _ZT_EXCEPT_H_ */

