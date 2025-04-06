/*
Copyright (C) 2024-2025 Ahmad Ismail
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <grp.h>
#include <signal.h>
#include <glob.h>
#include <stdbool.h>

#include "common.h"

#define BAT_CTRL_GLOB "/sys/class/power_supply/BAT?/charge_control_end_threshold"
#define CONFIG_FILE "/etc/batteryd.conf"
#define SOCKET_PATH "/run/batteryd"

void clean_exit(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        unlink(SOCKET_PATH);
        exit(0);
    }
}

char *get_battery_path()
{
    glob_t matches;
    int status = glob(BAT_CTRL_GLOB, 0, NULL, &matches);
    char *path = strdup(matches.gl_pathv[0]);
    globfree(&matches);

    switch (status)
    {
    case 0:
        return path;

    case GLOB_NOMATCH:
        fputs("No battery charge threshold control found\n", stderr);
    case GLOB_NOSPACE:
    case GLOB_ABORTED:
    default:
        return NULL;
    }
}

int set_battery_charge_threshold(int8_t threshold, bool persistent)
{
    if (threshold < 50)
        return VALUE_TOO_SMALL;
    else if (threshold > 100)
        return VALUE_TOO_LARGE;
    FILE *control;
    char *path = get_battery_path();

    if (path == NULL)
        return SYSTEM_FAILURE;

    control = fopen(path, "w");
    free(path);
    if (control == NULL)
        return SYSTEM_FAILURE;

    int chars_written = fprintf(control, "%" PRId8, threshold);
    fclose(control);
    if (chars_written <= 0)
    {
        perror("Failed to set threshold");
        return SYSTEM_FAILURE;
    }

    // Save to configuration file
    // If not persistent, just return success since we won't be here if the operation failed earlier
    if (persistent)
    {
        FILE *config = fopen(CONFIG_FILE, "w");
        if (config == NULL)
        {
            perror("Unable open configuration file");
            return SYSTEM_FAILURE;
        }
        chars_written = fprintf(config, "%" PRId8, threshold);
        fclose(config);
        if (chars_written <= 0)
        {
            perror("Failed to write configuration file");
            return SYSTEM_FAILURE;
        }
        else
            return SUCCESS;
    }
    else
        return SUCCESS;
}

int get_battery_threshold()
{
    FILE *control;
    char *path = get_battery_path();

    if (path == NULL)
        return -1;

    control = fopen(path, "r");
    free(path);
    if (control == NULL)
    {
        perror("Failed to open battery threshold control file");
        return -1;
    }
    int threshold;
    fscanf(control, "%d", &threshold);
    fclose(control);

    return threshold;
}

int restore_config()
{
    FILE *config = fopen(CONFIG_FILE, "r");
    if (config == NULL)
    {
        perror("Unable to load config");
        return 1;
    }
    int threshold, status;
    status = fscanf(config, "%d", &threshold);
    fclose(config);
    if (status == 1)
    {
        // No need to set persistent to true, the file is already there with the value
        status = set_battery_charge_threshold(threshold, false);
        switch (status)
        {
        case VALUE_TOO_SMALL:
        case VALUE_TOO_LARGE:
            fputs("Incorrect values in configuration file, resetting...\n", stderr);
            set_battery_charge_threshold(100, true);
            return 1;
        case SYSTEM_FAILURE:
            exit(1);
        default:
            printf("Set battery charge threshold to %d (from configuration)\n", threshold);
            return 0;
        }
    }
    else
    {
        // Someone corrupted my config, fix it
        fputs("Configuration file is apparently broken, resetting...\n", stderr);
        set_battery_charge_threshold(100, true);
        return 2;
    }
}

int main(void)
{
    restore_config();
    struct sockaddr_un srv_socket;
    int srv_fd;

    /*
     * For portability clear the whole structure, since some
     * implementations have additional (nonstandard) fields in
     * the structure. (this is according to man unix(7))
     */
    memset(&srv_socket, 0, sizeof(srv_socket));
    srv_socket.sun_family = AF_UNIX;
    strcpy(srv_socket.sun_path, SOCKET_PATH);

    srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (srv_fd == -1)
    {
        perror("Failed to create server socket");
        return 1;
    }

    // Get the GID of the batteryd group to allow access of group members to socket
    struct group *grp;
    grp = getgrnam("batteryd");
    if (grp == NULL)
    {
        fputs("Failed to get group batteryd\n", stderr);
        return 2;
    }

    if (bind(srv_fd, (struct sockaddr *)(&srv_socket), sizeof(srv_socket)) == -1)
    {
        perror("Failed to bind the unix socket");
        return 1;
    }
    // Set the socket to be rw for root and the batteryd group
    chmod(SOCKET_PATH, 660);
    chown(SOCKET_PATH, 0, grp->gr_gid);
    // Install signal handlers for clean termination
    signal(SIGINT, clean_exit);
    signal(SIGTERM, clean_exit);

    if (listen(srv_fd, 1) == -1)
    {
        perror("Failed to start listener");
        return 1;
    }
    while (1)
    {
        int client_fd = accept(srv_fd, NULL, NULL);
        int8_t opcode;
        int8_t threshold;
        bool persistent;

        // Read the operation requested by the user, check at the same time that the client didn't die unexpectedly
        if (read(client_fd, &opcode, 1) == 1)
        {
            switch (opcode)
            {
            case SET_THRESHOLD:
                if (!(read(client_fd, &threshold, 1) < 1 || read(client_fd, &persistent, 1) < 1))
                {
                    int8_t status = set_battery_charge_threshold(threshold, persistent);
                    write(client_fd, &status, 1);
                    // Rate limiting to prevent a faulty user space process from thrashing the battery controller
                    sleep(2);
                }
                break;
            case GET_THRESHOLD:
                threshold = get_battery_threshold();
                if (send(client_fd, &threshold, 1, MSG_NOSIGNAL) < 1)
                    perror("Failed to reply to client");
                break;
            case RELOAD_CONFIG:
                restore_config();
                threshold = get_battery_threshold();
                if (send(client_fd, &threshold, 1, MSG_NOSIGNAL) < 1)
                    perror("Failed to reply to client");
            }
        }
    }
    return 0;
}