/*
Copyright (C) 2024-2025 Ahmad Ismail
SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <glob.h>

#include "common.h"

#define BAT_CTRL_GLOB "/sys/class/power_supply/BAT?/charge_control_end_threshold"

int client_fd;

// Connects to the batteryd socket and places the file descriptor in the global client_fd
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
    int8_t operation, server_response;
    bool set_threshold = false, persist = true, get_threshold = false, wants_help = false, remove_till_boot = false,
         reload_cfg = false;
    char *user_input;
    while ((c = getopt(argc, argv, "s:tfghr")) != -1)
    {
        switch (c)
        {
        case 's':
            set_threshold = true;
            user_input = strdup(optarg);
            break;
        case 't':
            persist = false;
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
        case 'r':
            if (optarg != NULL)
                fputs("Option -r doesn't expect an argument\n", stderr);
            reload_cfg = true;
            break;
        case '?':
            fputs("Incorrect usage, type \"batteryctl -h\" for help\n", stderr);
            return 1;
        }
    }

    if (wants_help + get_threshold + set_threshold + reload_cfg != 1)
    {
        fputs("Expected one option, type \"batteryctl -h\" for help\n", stderr);
        return 1;
    }

    if (set_threshold)
    {
        int threshold;
        operation = SET_THRESHOLD;

        if (remove_till_boot)
        {
            // Requested by the -f option (fully charge until next reboot)
            persist = false;
            threshold = 100;
        }
        else
        {
            // Regular "update threshold" operation
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

        if (write(client_fd, &operation, 1) < 1 || write(client_fd, &threshold, 1) < 1 || write(client_fd, &persist, 1) < 1)
        {
            fputs("Failed to write to the server socket!\n", stderr);
            return 1;
        }
        read(client_fd, &server_response, 1);
        switch (server_response)
        {
        case SUCCESS:
            if (remove_till_boot)
                puts("Battery charge threshold removed until next restart");
            else if (persist)
                printf("Battery charge threshold set to %d%%\n", threshold);
            else
                printf("Battery charge threshold set to %d%% until next restart\n", threshold);
            break;
        case VALUE_TOO_SMALL:
            fputs("Failed to set threshold: value too small, try value > 49\n", stderr);
            break;
        // This should never happen in theory as we validate the threshold earlier
        case VALUE_TOO_LARGE:
            fputs("Failed to set threshold: value too large, try value <= 100\n", stderr);
            break;
        case SYSTEM_FAILURE:
            fputs("Something went wrong with the service, check batteryd's logs\n", stderr);
            break;
        default:
            fputs("Unexpected response, please check batteryd service for malfunction.\n", stderr);
            break;
        }
        return server_response;
    }
    else if (wants_help)
    {
        puts("Available options:");
        puts("-s <value>   Set the battery charge threshold");
        puts("-t           Used alongside -s to make the change temporary");
        puts("-g           Get the current charge threshold");
        puts("-f           Remove the battery charge threshold until the next boot");
        puts("-r           Reload threshold from configuration file");
        puts("-h           Print this help prompt");
        return 0;
    }
    else if (get_threshold)
    {
        u_int8_t threshold;
        operation = GET_THRESHOLD;
        connect_to_service();
        if (write(client_fd, &operation, 1) < 1)
        {
            fputs("Failed to write to the server socket!\n", stderr);
            return 1;
        }

        if (read(client_fd, &threshold, 1) < 1)
        {
            fputs("Failed to get threshold!\n", stderr);
            return 1;
        }

        printf("Current charge threshold is %d%%\n", threshold);

        return 0;
    }
    else if (reload_cfg)
    {
        operation = RELOAD_CONFIG;
        connect_to_service();
        if (write(client_fd, &operation, 1) < 1)
        {
            return 1;
        }
        if (read(client_fd, &server_response, 1) < 1)
        {
            return 1;
        }

        printf("Reloaded battery threshold successfuly, now at %d%%\n", server_response);
    }
    return 0;
}
