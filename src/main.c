
#include <stdio.h>
#include "spawn.h"
#include "strformat.h"

int main(int argc, char *argv[])
{
    spawn_t sp;
    char buffer[256];
    size_t rd;
    strformat_symbs_t* symbs;
    strformat_t* fmt;
    size_t nb;
    if(argc < 2) {
        printf("Too few arguments.\n");
        return 1;
    }

    /* %s -> the data, %n -> the number of the paquet. */
    symbs = strformat_symbols("sn");
    fmt   = strformat_parse(symbs, argv[1]);
    if(!fmt) {
        printf("Couldn't parse format string.\n");
        return 1;
    }

    sp = spawn_create(argv + 2);
    if(!spawn_ok(sp)) {
        printf("Couldn't spawn argument.\n");
        return 1;
    }

    nb = 0;
    while((rd = spawn_read(sp, buffer, 256)) > 0) {
        ++nb;
        buffer[rd] = '\0';
        strformat_set(symbs, 's', buffer);
        sprintf(buffer, "%li", nb);
        strformat_set(symbs, 'n', buffer);

        printf("%s\n", strformat_get(fmt));
    }

    if(!spawn_ended(sp))
        spawn_wait(sp);
    spawn_close(&sp);

    strformat_destroy(fmt);
    strformat_symbols_destroy(symbs);

    return 0;
}

