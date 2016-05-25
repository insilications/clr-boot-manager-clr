/*
 * This file is part of clr-boot-manager.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cli.h"
#include "config.h"
#include "hashmap.h"
#include "util.h"

#include "ops/timeout.h"
#include "ops/update.h"

static SubCommand cmd_update;
static SubCommand cmd_help;
static SubCommand cmd_version;
static SubCommand cmd_set_timeout;
static SubCommand cmd_get_timeout;
static char *binary_name = NULL;
static CbmHashmap *g_commands = NULL;
static bool explicit_help = false;

/**
 * Print usage
 */
static bool print_usage(int argc, char **argv)
{
        CbmHashmapIter iter;
        const char *id = NULL;
        const SubCommand *command = NULL;

        if (argc > 1) {
                fprintf(stderr, "Usage: %s help [topic]\n", binary_name);
                return false;
        } else if (argc == 1 && !explicit_help) {
                command = cbm_hashmap_get(g_commands, argv[0]);
                if (!command) {
                        fprintf(stderr, "Unknown topic '%s'\n", argv[0]);
                        return false;
                }
                fprintf(stdout,
                        "Usage: %s %s%s\n",
                        binary_name,
                        command->name,
                        command->usage ? command->usage : "");
                fprintf(stdout, "\n%s\n", command->help ? command->help : command->blurb);
                return true;
        }

        fprintf(stderr, "Usage: %s\n\n", binary_name);

        cbm_hashmap_iter_init(g_commands, &iter);
        while (cbm_hashmap_iter_next(&iter, (void **)&id, (void **)&command)) {
                fprintf(stdout, "%15s - %s\n", id, command->blurb);
        }

        return true;
}

static bool print_version(__attribute__((unused)) int argc, __attribute__((unused)) char **argv)
{
        fprintf(stdout,
                PACKAGE_NAME " - version " PACKAGE_VERSION
                             "\n\
\n\
Copyright \u00A9 2016 Intel Corporation\n\n\
" PACKAGE_NAME " is free software; you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation; either version 2 of the License, or\n\
(at your option) any later version.\n");
        return true;
}

int main(int argc, char **argv)
{
        atexit(dump_file_descriptor_leaks);

        autofree(CbmHashmap) *commands = NULL;
        const char *command = NULL;
        SubCommand *s_command = NULL;

        binary_name = argv[0];

        commands = cbm_hashmap_new_full(string_hash, string_compare, NULL, NULL);
        if (!commands) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }
        g_commands = commands;

        /* Currently our only "real" command */
        cmd_update = (SubCommand){
                .name = "update",
                .blurb = "Perform post-update configuration of the system",
                .help =
                    "Automatically install any newly discovered kernels on the file system\n\
and register them with the UEFI boot manager. Older, unused kernels will\n\
be automatically garbage collected.\n\
\n\
If necessary, the bootloader will be updated and/or installed during this\n\
time.",
                .callback = cbm_command_update,
                .usage = " [--path=/path/to/filesystem/root]",
                .requires_root = true
        };

        if (!cbm_hashmap_put(commands, cmd_update.name, &cmd_update)) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }

        /* Set the timeout */
        cmd_set_timeout = (SubCommand){
                .name = "set-timeout",
                .blurb = "Set the timeout to be used by the bootloader",
                .help = "Set the default timeout to be used by" PACKAGE_NAME
                        " when using\n\
the \"update\" command.\n\
This integer value will be used when next configuring the bootloader, and is used\n\
to forcibly delay the system boot for a specified number of seconds.",
                .callback = cbm_command_set_timeout,
                .usage = " [--path=/path/to/filesystem/root]",
                .requires_root = true,
        };

        if (!cbm_hashmap_put(commands, cmd_set_timeout.name, &cmd_set_timeout)) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }

        /* Get the timeout */
        cmd_get_timeout = (SubCommand){
                .name = "get-timeout",
                .blurb = "Get the timeout to be used by the bootloader",
                .help = "Get the default timeout to be used by" PACKAGE_NAME
                        " when using\n\
the \"update\" command.\n\
This integer value will be used when next configuring the bootloader, and is used\n\
to forcibly delay the system boot for a specified number of seconds.",
                .callback = cbm_command_get_timeout,
                .usage = " [--path=/path/to/filesystem/root]",
                .requires_root = false,
        };

        if (!cbm_hashmap_put(commands, cmd_get_timeout.name, &cmd_get_timeout)) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }

        /* Version */
        cmd_version = (SubCommand){
                .name = "version",
                .blurb = "Print the version and quit",
                .callback = print_version,
                .usage = NULL,
                .help = NULL,
                .requires_root = false,
        };
        if (!cbm_hashmap_put(commands, cmd_version.name, &cmd_version)) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }

        /* Help */
        cmd_help = (SubCommand){.name = "help",
                                .blurb = "Show help message",
                                .callback = print_usage,
                                .usage = " [topic]",
                                .help = NULL,
                                .requires_root = false };

        if (!cbm_hashmap_put(commands, cmd_help.name, &cmd_help)) {
                DECLARE_OOM();
                return EXIT_FAILURE;
        }

        if (argc < 2) {
                fprintf(stderr,
                        "Usage: %s [command]\nRe-run with -h for a list of supported commands\n",
                        binary_name);
                return EXIT_FAILURE;
        }

        /* Centralise help access */
        if (streq(argv[1], "-h") || streq(argv[1], "--help")) {
                command = cmd_help.name;
                explicit_help = true;
        } else if (streq(argv[1], "-v") || streq(argv[1], "--version")) {
                command = cmd_version.name;
        } else {
                command = argv[1];
        }

        /* discard prog name */
        ++argv;
        --argc;

        s_command = cbm_hashmap_get(commands, command);
        if (!s_command) {
                fprintf(stderr, "Unknown command: %s\n", command);
                return EXIT_FAILURE;
        }

        if (!s_command->callback) {
                fprintf(stderr, "%s is not yet implemented\n", s_command->name);
                return EXIT_FAILURE;
        }

        if (s_command->requires_root && geteuid() != 0) {
                fprintf(stderr,
                        "%s \'%s\' requires root permissions to execute. Try again with sudo\n",
                        binary_name,
                        s_command->name);
                return false;
        }

        /* Invoke with discarded subcommand */
        if (!s_command->callback(--argc, ++argv)) {
                return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
