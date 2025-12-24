#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cc_inputs(&cmd, "src/main.c");
    nob_cc_output(&cmd, "compute");

    if (!nob_cmd_run(&cmd))
        return 1;

    nob_cmd_append(&cmd, "./compute");

    if (!nob_cmd_run(&cmd))
        return 1;
}
