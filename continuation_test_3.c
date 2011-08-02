/*
 * Continuation repeater example. which just repeats 2 continuations one after the other
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "continuation_player.h"

struct scope_repeater
{
    int cur_index;
    int len;
    char *arr;
};

static void continuation_repeater_release(void *arg, int last_cont)
{
    struct scope_repeater *scope = arg;
    if(scope->arr) free(scope->arr);
    free(scope);
}

static void do_bday(void *arg)
{
    struct scope_repeater *scope = arg;
    int index = scope->cur_index;
    if(!scope->len)
    {
        scope->arr = strdup("a" "p" " " "b" "r" "h" "a" " " "k" "r" "h" "c" " " "!" "\n");
        scope->len = strlen(scope->arr);
    }
    scope->cur_index = (scope->cur_index + 1) % scope->len;
    putc(scope->arr[index], stdout);
}

static void do_happy(void *arg)
{
    struct scope_repeater *scope = arg;
    int index = scope->cur_index;
    if(!scope->len)
    {
        scope->arr = strdup("h" "p" "y" " " "i" "t" "d" "y" " " "a" "t" "i" "k" " " "!");
        scope->len = strlen(scope->arr);
    }
    scope->cur_index = (scope->cur_index + 1) % scope->len;
    putc(scope->arr[index], stdout);
}

static __inline__ struct scope_repeater *make_scope(void)
{
   return calloc(1, sizeof(struct scope_repeater));
}

static void continuation_repeater(void)
{
    continuation_block_t continuations[2] = {
        {.block = do_happy, .arg = make_scope()},
        {.block = do_bday, .arg = make_scope()},
    };

    int c = open_continuation(continuations, sizeof(continuations)/sizeof(continuations[0]), 
                              continuation_repeater_release, CONT_REPEATER);
    mark_continuation(c);
    /*
     * Just play the marked continuations.
     */
    continuation_player();
}

int main(int argc, char **argv)
{
    continuation_repeater();
    return 0;
}
