// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <orbit/util/macros.h>

#include <orbit/orbiter/version.h>

#include <orbit/orbiter/config.h>

using namespace orbiter;

constexpr Config DefaultConfig = {
    .argv = nullptr,

    .argc = 0,
    .file = -1,
    .cmd = -1,

    .ost_max = -1,
    .vc_max = -1,
    .fiber_ssize = -1,
    .fiber_pool = -1,

    .interactive = true,
    .quiet = false
};

const Config *orbiter::kConfigDefault = &DefaultConfig;

// PARSER

struct ReadOpStatus {
    char **argv;
    char *arg_cur;
    char *argument;

    int argc;
    int argc_cur;
};

struct ReadOpLong {
    const char *name;
    bool harg;
    char rt;
};

#define BAD_OPT     0xFF
#define FEW_ARGS    0xFE
#define NONOPT      0xFD
#define ISLOPT      0xFC

int ReadOp(ReadOpStatus *status, const char *opts, const ReadOpLong *lopt, int llopt, char tr) {
    const char *subopt;
    int ret;

    status->arg_cur = status->argv[status->argc_cur];
    status->argument = nullptr;

    if (status->argc_cur >= status->argc)
        return -1;

    if (*status->arg_cur == tr) {
        status->arg_cur++;
        if (*status->arg_cur == tr && lopt != nullptr) {
            status->arg_cur++;
            for (int i = 0; i < (llopt / sizeof(ReadOpLong)); i++) {
                if (strcmp(lopt[i].name, status->arg_cur) == 0) {
                    ret = lopt[i].rt == -1 ? ISLOPT : lopt[i].rt;
                    status->argc_cur++;

                    if (!lopt[i].harg)
                        return ret;

                    if (status->argc_cur >= status->argc || *status->argv[status->argc_cur] == tr)
                        return FEW_ARGS;

                    status->argument = status->argv[status->argc_cur++];
                    return ret;
                }
            }
            return BAD_OPT;
        }

        ret = (int) ((unsigned char) *status->arg_cur);
        if ((subopt = strchr(opts, *status->arg_cur)) != nullptr) {
            status->argc_cur++;
            if (*++subopt == '!') {
                if (status->argc_cur >= status->argc || *status->argv[status->argc_cur] == tr)
                    return FEW_ARGS;

                status->argument = status->argv[status->argc_cur];
                status->argc_cur++;
            }
            return ret;
        }
        return BAD_OPT;
    }

    status->argument = status->argv[status->argc_cur++];
    return NONOPT;
}

// ---------------------------------------------------------------------------------------------

static constexpr char usage_line[] =
        "usage: %s [option] [-c cmd | file] [arg...]\n";

static constexpr char usage[] =
        "\nOptions and arguments:\n"
        "-c cmd         : program string\n"
        "-h, --help     : print this help message and exit\n"
        "-i             : start interactive mode after running script\n"
        "--nogc         : disable garbage collector\n"
        "-O             : set optimization level (0-3 -- 0: disabled, 3: hard)\n"
        "-q             : don't print version messages on interactive startup\n"
        "-v, --version  : print Orbit version and exit\n";

static constexpr char usage_env[] =
        "\nEnvironment variables:\n"
        "ORBIT_MAXVC     : value that controls the number of OS threads that can execute Orbit code simultaneously.\n"
        "                  The default value of ORBIT_MAXVC is the number of CPUs visible at startup.\n"
        "ORBIT_PATH      : augment the default search path for modules. One or more directories separated by "
        #ifdef _ORBIT_PLATFORM_WINDOWS
        "';' "
        #else
        "':' "
        #endif
        "as the shell's PATH.\n"
        "ORBIT_STARTUP   : specifies the script that must be run before the interactive prompt is shown for "
        "the first time.\n";

// ---------------------------------------------------------------------------------------------

void Help(const char *name) {
    printf(usage_line, name);
    puts(usage);
    puts(usage_env);
}

void ParseEnvs(Config *config) {
   // TODO
}

bool orbiter::ConfigInit(Config *config, int argc, char **argv) {
    ReadOpLong lopt[] = {
            {"help",    false, 'h'},
            {"version", false, 'v'},
    };
    ReadOpStatus status = {};

    int ret = 0;
    bool interactive = false;

    status.argv = argv + 1;
    status.argc = argc - 1;

    while (ret != -1 && (ret = ReadOp(&status, "c!hqv", lopt, sizeof(lopt), '-')) != -1) {
        switch (ret) {
            case 'c':
                config->cmd = status.argc_cur;
                config->interactive = interactive;
                break;
            case 'h':
                Help(*argv);
                exit(EXIT_SUCCESS);
            case 'q':
                config->quiet = true;
                break;
            case 'v':
                printf("Orbit %d.%d.%d(%s)\n", OR_MAJOR, OR_MINOR, OR_PATCH, OR_RELEASE_LEVEL);
                exit(EXIT_SUCCESS);
            case ISLOPT:
                break;
            case BAD_OPT:
                fprintf(stderr, "unrecognized option: %s\n", status.argv[status.argc_cur]);
                exit(EXIT_FAILURE);
            case FEW_ARGS:
                fprintf(stderr, "option %s expected an argument\n", status.argv[status.argc_cur - 1]);
                exit(EXIT_FAILURE);
            default:
                // NON OPT
                config->file = status.argc_cur;
                config->interactive = interactive;

                config->argc = argc - status.argc_cur;
                config->argv = argv + status.argc_cur;

                ret = -1;
        }
    }

    ParseEnvs(config);

    return true;
}



