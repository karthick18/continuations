/*
 * A continuation example with closure.
 * To test:
 *
 * make or make ARCH=x86 for 32 bit
 * ./continuation  
 *  would test with creating 10 continuations each stacking 3 levels of continuations before unwinding
 * ./continuation 20 
 * would test with 20 continuations
 *
 * These principles could be used to create a single threaded async request server.
 */

#ifndef __linux__
#error "fuck off as your arch. is not supported"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include <pthread.h>
#include "list.h"

#if !defined(MAP_32BIT) && !defined(__i386__)
#error "MAP_32BIT not defined for 64 bit"
#endif

#define _CONTINUATION_C_

#include "thunk.h"

struct scope
{
    void *args;
};

typedef void (*closure_t)(void);

struct continuation
{
    struct list_head list;
    closure_t thunk;
    int continuation;
};

#define likely(expr) __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)

#define __ALIGN(v, a) ( ( (v) + (a) - 1) & ~( (a) - 1 ) )
#define CONTINUATION_MAP_BITS (1024) /* 1024 simultaneous requests/continuations */
#define CONTINUATION_MAP_BYTES ( __ALIGN(CONTINUATION_MAP_BITS, 8) >>  3 )
#define CONTINUATION_MAP_SIZE  CONTINUATION_MAP_BYTES 
#define CONTINUATION_MAP_WORDS (__ALIGN(CONTINUATION_MAP_SIZE, sizeof(unsigned int)) >> 2 )

struct continuation_map
{
    struct list_head *continuations;
    unsigned int map[CONTINUATION_MAP_WORDS];
};

static struct continuation_map continuation_map = {
    .map = { [0 ... CONTINUATION_MAP_WORDS-1] = 0 },
};

static int __cfz(unsigned int *map, int bit)
{
    if(unlikely(bit >= CONTINUATION_MAP_BITS)) return -1;
    map[bit >> 5] &= ~(1 << (bit & 31));
    return 0;
}

static int __ffz(unsigned int *map, unsigned int size)
{
    int i;
    static int ffz_map[16] = { 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, -1 };
    for(i = 0; i < size; ++i)
    {
        unsigned int offset = 0;
        unsigned int mask = map[i] & 0xffffffffU;
        if(mask == 0xffffffffU) continue;
        if( ( mask & 0xffff ) == 0xffff )
        {
            offset += 16;
            mask >>= 16;
        }
        if( (mask & 0xff) == 0xff)
        {
            offset += 8;
            mask >>= 8;
        }
        if( (mask & 0xf) == 0xf)
        {
            offset += 4;
            mask >>= 4;
        }
        
        offset += ffz_map[ mask & 0xf ];
        map[i] |= ( 1 << offset );
        i = (i << 5) + offset;
        if( unlikely(i >= CONTINUATION_MAP_BITS))
            return -1;
        return i;
    }
    return -1;
}

static __inline__ void init_continuation_map(void)
{
    register int i;
    continuation_map.continuations = calloc(CONTINUATION_MAP_BITS, sizeof(*continuation_map.continuations));
    assert(continuation_map.continuations);
    for(i = 0; i < CONTINUATION_MAP_BITS; ++i)
    {
        LIST_HEAD_INIT(continuation_map.continuations + i);
    }
}

static int get_continuation(struct continuation_map *map)
{
    return __ffz(map->map, sizeof(map->map)/sizeof(map->map[0]));
}

static int clear_continuation(struct continuation_map *map, int continuation)
{
    return __cfz(map->map, continuation);
}

static __inline__ struct list_head *get_continuation_queue(int cont)
{
    if(unlikely(cont >= CONTINUATION_MAP_BITS)) return NULL;
    return continuation_map.continuations + cont;
}

/*
 * Take a scope and the function block to be bound to the scope.
 */
static closure_t make_closure(void *block, void *arg)
{
    struct thunk *thunk = (struct thunk *)mmap(0, sizeof(*thunk) + sizeof(thunk), PROT_READ | PROT_WRITE | PROT_EXEC, 
                                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                                               -1, 0);
    if((char*)thunk == MAP_FAILED)
        return NULL;
    *thunk = initialize_thunk;
    thunk->scope = calloc(1, sizeof(*thunk->scope));
    if(unlikely(!thunk->scope))
    {
        goto out_free;
    }
    thunk->scope->args = arg;
    thunk->call_offset = (long)block - (long)&thunk->UNWIND_OP;
    return (closure_t)thunk;

    out_free:
    munmap(thunk, sizeof(*thunk) + sizeof(thunk));
    return (closure_t)NULL;
}

static void free_closure(closure_t closure)
{
    struct thunk *thunk = (struct thunk*)closure;
    int err = 0;
    free(thunk->scope);
    err = munmap((void*)closure, sizeof(*thunk) + sizeof(thunk));
    assert(err == 0);
}


/*
 * Stack it into a reclaim list since it could be released in the context of the closure itself.
 * So let it be released async.
 * We cannot write to the header of the thunk for the reclaim list as that contains the closure code.
 * So cannot overwrite ourselves.
 */
static struct thunk *reclaim_list;

static void release_closure(closure_t closure)
{
    struct thunk *thunk = (struct thunk *)closure + 1;
    *(struct thunk**)thunk = reclaim_list;
    reclaim_list = thunk;
}

static void reclaim_closure(void)
{
    struct thunk *iter = reclaim_list;
    struct thunk *next = NULL;
    while(iter)
    {
        struct thunk *thunk = iter - 1;
        next = *(struct thunk**)iter;
        free_closure((closure_t)thunk);
        iter = next;
    }
    reclaim_list = NULL;
}

static int stack_continuation(int cont, void *block, void *args)
{
    struct list_head *queue = get_continuation_queue(cont);
    struct continuation *continuation = NULL;
    int err = -1;
    if(unlikely(!queue)) goto out;
    continuation = calloc(1, sizeof(*continuation));
    assert(continuation);
    continuation->thunk = make_closure(block, args);
    if(unlikely(!continuation->thunk)) goto out_free;
    /*
     * stack the continuation.
     */
    list_add(&continuation->list, queue);
    err = 0;
    goto out;

    out_free:
    free(continuation);

    out:
    return err;
}

static struct continuation *pop_continuation(int cont)
{
    struct list_head *queue = get_continuation_queue(cont);
    struct continuation *continuation;
    if(unlikely(!queue)) return NULL;
    if(LIST_EMPTY(queue)) return NULL;
    continuation = list_entry(queue->next, struct continuation, list);
    list_del(&continuation->list);
    if(LIST_EMPTY(queue))
    {
        clear_continuation(&continuation_map, cont);
    }
    return continuation;
}

static int remove_continuation(int cont)
{
    struct continuation *continuation;
    continuation = pop_continuation(cont);
    if(unlikely(!continuation)) return -1;
    release_closure(continuation->thunk);
    free(continuation);
    return 0;
}

/*
 * Just return the continuation at the top of the list
 */
static struct continuation *peek_continuation(int cont)
{
    struct list_head *queue = get_continuation_queue(cont);
    struct continuation *continuation;
    if(unlikely(queue == NULL)) return NULL;
    if(LIST_EMPTY(queue)) return NULL;
    continuation = list_entry(queue->next, struct continuation, list);
    return continuation;
}

static int run_continuation(int cont)
{
    struct continuation *continuation;
    continuation = peek_continuation(cont);
    if(!continuation) return -1;
    continuation->thunk(); /* run the continuation block*/
    return 0;
}

struct continuation_scope
{
#define WIND (0x1)
#define UNWIND (0x2)
    int continuation;
    int state;
};

static struct continuation_scope *make_scope(int continuation)
{
    struct continuation_scope *scope = calloc(1, sizeof(*scope));
    assert(scope);
    scope->continuation = continuation;
    scope->state |= WIND;
    return scope;
}

#define PRINT_CONTINUATION(cont) do {                                   \
    printf("Inside continuation id [%d] with state [%d] in function [%s]\n", \
           (cont)->continuation, (cont)->state, __FUNCTION__);          \
} while(0)

static void continuation_2(struct scope *scope)
{
    struct continuation_scope *cont = scope->args;
    PRINT_CONTINUATION(cont);
    /*
     * unwind 
     */
    if(cont->state & WIND)
    {
        cont->state &= ~WIND;
        cont->state |= UNWIND;
        remove_continuation(cont->continuation);
    }
}

static void continuation_1(struct scope *scope)
{
    struct continuation_scope *cont = scope->args;
    PRINT_CONTINUATION(cont);
    if(cont->state & WIND)
    {
        stack_continuation(cont->continuation, (void*)continuation_2, (void*)cont);
    }
    else 
    {
        remove_continuation(cont->continuation);
    }
}

static void continuation_0(struct scope *scope)
{
    struct continuation_scope *cont = scope->args;
    PRINT_CONTINUATION(cont);
    if(cont->state & WIND)
    {
        stack_continuation(cont->continuation, (void*)continuation_1, (void*)cont);
    }
    else
    {
        remove_continuation(cont->continuation);
        printf("Continuation unwind for [%d]\n", cont->continuation);
        free(cont); /* last unwind*/
    }
}

static void *continuation_player(void *unused)
{
    register int i;
    for(;;)
    {
        int run = 0;
        /*
         * Walk the bitmap and run the set continuations.  
         * Break when all are cleared.
         */
        for(i = 0; i < CONTINUATION_MAP_WORDS; ++i)
        {
            unsigned int mask = continuation_map.map[i];
            register int j;
            if(!mask) continue;
            for(j = 0; j < 32; ++j)
            {
                if( mask & ( 1 << j) )
                {
                    run = 1;
                    run_continuation((i << 5) + j);
                }
                
            }
        }
        /*
         * Reclaim closures if any.
         */
        reclaim_closure();
        if(!run) 
        {
            fprintf(stderr, "All continuations have been run\n");
            break;
        }
    }
    return NULL;
}

static void continuation_test(int continuations)
{
    int i;
    pthread_attr_t attr;
    pthread_t tid;
    init_continuation_map();
    pthread_attr_init(&attr);
    for(i = 0; i < continuations; ++i)
    {
        int c = get_continuation(&continuation_map);
        assert(c >= 0);
        assert(stack_continuation(c, (void*)continuation_0, make_scope(c)) == 0);
    }
    pthread_create(&tid, &attr, continuation_player, NULL);
    pthread_join(tid, NULL);
}

int main(int argc, char **argv)
{
    int c = 10;
    if(argc > 1)
        c = atoi(argv[1]);
    if(!c) c = 10;
    if(c > CONTINUATION_MAP_BITS) c = CONTINUATION_MAP_BITS;
    continuation_test(c);
    return 0;
}
