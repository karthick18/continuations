#ifndef _CONTINUATION_C_
#error "This header is meant to be included only from continuations file"
#endif

#if defined (__x86_64__)

#define UNWIND_OP pop_op

struct thunk
{
    unsigned char push_op;  /* push rdi */
    unsigned char mov_op[2]; /* mov $env, rdi */
    struct scope *scope; 
    unsigned char call_op; /* callq  : would work as x86_64 args are passed in rdi */
    int call_offset; /*$offset => function - address_of_call_offset*/
    unsigned char pop_op; /* popq rdi */
    unsigned char ret_op; /* ret */
} __attribute__((packed));

static struct thunk initialize_thunk = { .push_op =  0x57, .mov_op = {0x48, 0xbf},
                                      .call_op = 0xe8, .call_offset = 0,
                                      .pop_op = 0x5f, .ret_op = 0xc3
};
#elif defined (__i386__)

#undef MAP_32BIT
#define MAP_32BIT (0)
#define UNWIND_OP add_op[0]

struct thunk
{
    unsigned char push_op;  /* push eax */
    unsigned char mov_op; /* mov $env, eax */
    struct scope *scope; 
    unsigned char push_op2; /* push eax or arg into the stack */
    unsigned char call_op; /* call */
    int call_offset; /*$offset => function - address_of_call_offset*/
    unsigned char add_op[3]; /* add $4, esp */
    unsigned char pop_op; /* pop eax */
    unsigned char ret_op; /* ret */
} __attribute__((packed));

static struct thunk initialize_thunk = { .push_op =  0x50, .mov_op = 0xb8, .push_op2 = 0x50,
                                      .call_op = 0xe8, .call_offset = 0, 
                                      .add_op = {0x83, 0xc4, 0x4 },
                                      .pop_op = 0x58, .ret_op = 0xc3
};

#else

#error "You are a fucking loser"

#endif
