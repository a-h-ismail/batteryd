/*
Copyright (C) 2024 Ahmad Ismail
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <glob.h>

#define BAT_CTRL_GLOB "/sys/class/power_supply/BAT?/charge_control_end_threshold"

enum service_status
{
    SUCCESS,
    VALUE_TOO_SMALL,
    VALUE_TOO_LARGE,
    SYSTEM_FAILURE
};

int client_fd;

void connect_to_service()
{
    struct sockaddr_un srv_socket;
    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        fputs("Failed to create socket!\n", stderr);
        exit(1);
    }

    /*
     * For portability clear the whole structure, since some
     * implementations have additional (nonstandard) fields in
     * the structure. (this is according to man unix(7))
     */
    memset(&srv_socket, 0, sizeof(srv_socket));

    srv_socket.sun_family = AF_UNIX;
    strcpy(srv_socket.sun_path, "/run/batteryd");

    int status = connect(client_fd, (struct sockaddr *)&srv_socket, sizeof(srv_socket));
    if (status == -1)
    {
        perror("Failed to connect to batteryd service");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    char c;
    int8_t server_response;
    bool set_threshold = false, get_threshold = false, wants_help = false, remove_till_boot = false;
    char *user_input;
    while ((c = getopt(argc, argv, "s:fgh")) != -1)
    {
        switch (c)
        {
        case 's':
            set_threshold = true;
            user_input = strdup(optarg);
            break;
        case 'f':
            set_threshold = true;
            remove_till_boot = true;
            break;
        case 'g':
            if (optarg != NULL)
                fputs("Option -g doesn't expect an argument\n", stderr);
            get_threshold = true;
            break;
        case 'h':
            wants_help = true;
            break;
        case '?':
            fputs("Incorrect usage, type \"batteryctl -h\" for help\n", stderr);
            return 1;
        }
    }

    if (wants_help + get_threshold + set_threshold != 1)
    {
        fputs("Expected one option, type \"batteryctl -h\" for help\n", stderr);
        return 1;
    }

    if (set_threshold)
    {
        bool persist;
        int threshold;
        if (remove_till_boot)
        {
            persist = false;
            threshold = 100;
        }
        else
        {
            persist = false;
            if (user_input == NULL)
            {
                fputs("Expected a battery charge threshold. Example: batteryctl -s 80\n", stderr);
                return 1;
            }
            if (strlen(user_input) > 3)
            {
                fputs("Please input up to 3 digits\n", stderr);
                return 1;
            }
            threshold = atoi(user_input);
            if (threshold < 1 || threshold > 100)
            {
                fputs("Not a valid battery threshold\n", stderr);
                return 1;
            }
        }
        connect_to_service();

        if (write(client_fd, &threshold, 1) < 1 || write(client_fd, &persist, 1) < 1)
        {
            fputs("Failed to write to the server socket!\n", stderr);
            return 1;
        }
        read(client_fd, &server_response, 1);
        switch (server_response)
        {
        case SUCCESS:
            if (remove_till_boot)
                puts("Battery charge threshold removed until next boot");
            else
                printf("Battery charge threshold set to %d\n", threshold);
            break;
        case VALUE_TOO_SMALL:
            fputs("Failed to set threshold: value too small, try value > 49\n", stderr);
            break;
        // This should never happen in theory as we validate the threshold earlier
        case VALUE_TOO_LARGE:
            fputs("Failed to set threshold: value too large, try value <= 100\n", stderr);
            break;
        case SYSTEM_FAILURE:
            fputs("Something went wrong with the service, check batteryd's logn\n", stderr);
            break;
        default:
            fputs("Unexpected response, please check batteryd service for malfunction.\n", stderr);
            break;
        }
        return server_response;
    }
    if (wants_help)
    {
        puts("Available options:");
        puts("-s <value>   Set the battery charge threshold");
        puts("-g           Get the current charge threshold");
        puts("-f           Remove the battery charge threshold until the next boot");
        puts("-h           Print this help prompt");
        return 0;
    }
    if (get_threshold)
    {
        FILE *control;
        glob_t matches;
        int status = glob(BAT_CTRL_GLOB, 0, NULL, &matches);
        switch (status)
        {
        case GLOB_NOMATCH:
            fputs("No battery charge threshold control found\n", stderr);
        case GLOB_NOSPACE:
        case GLOB_ABORTED:
            return 1;
        case 0:
            control = fopen(matches.gl_pathv[0], "r");
            globfree(&matches);
            if (control == NULL)
            {
                perror("Failed to open battery threshold control file");
                return 1;
            }
            int threshold;
            fscanf(control, "%d", &threshold);
            printf("Current charge threshold is %d\n", threshold);
            return 0;
        }
    }
    return 1;
}