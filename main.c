 /**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * main.c
 */

#include <signal.h>
#include "system.h"
#include <stdio.h>
#include <string.h>

#define PROC_NET_DEV "/proc/net/dev"
#define PROC_MEMINFO "/proc/meminfo"
#define LINE_BUFFER_SIZE 1024
#define IFACE_NAME_SIZE 32

/* Define TRACE if not already defined */ 
#ifndef TRACE
#define TRACE(msg) fprintf(stderr, "Error: %s\n", msg)
#endif
/**
 * Needs:
 *   signal()
 */

static volatile int done;

static void
_signal_(int signum) {
    assert(SIGINT == signum);

    done = 1;
}

double
cpu_util(const char *s) {
    static unsigned sum_, vector_[7];
    unsigned sum, vector[7];
    const char *p;
    double util;
    uint64_t i;

    /*
      user
      nice
      system
      idle
      iowait
      irq
      softirq
    */

    if (!(p = strstr(s, " ")) ||
        (7 != sscanf(p,
                     "%u %u %u %u %u %u %u",
                     &vector[0],
                     &vector[1],
                     &vector[2],
                     &vector[3],
                     &vector[4],
                     &vector[5],
                     &vector[6]))) {
        return 0;
    }
    sum = 0.0;
    for (i = 0; i < ARRAY_SIZE(vector); ++i) {
        sum += vector[i];
    }
    util = (1.0 - (vector[3] - vector_[3]) / (double) (sum - sum_)) * 100.0;
    sum_ = sum;
    for (i = 0; i < ARRAY_SIZE(vector); ++i) {
        vector_[i] = vector[i];
    }
    return util;
}

/* get memory details */
unsigned long get_meminfo_value(const char *key) {
    FILE *file = fopen(PROC_MEMINFO, "r");
    char line[1024];
    unsigned long value = 0;

    if (!file) {
        TRACE("fopen() - Memory");
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            sscanf(line + strlen(key), "%lu", &value);
            break;
        }
    }

    fclose(file);
    return value;
}

/* function to calculate and display memory usage */
void memory_stats(void) {
    unsigned long mem_total = get_meminfo_value("MemTotal:");
    unsigned long mem_free = get_meminfo_value("MemFree:");

    if (mem_total == 0) {
        TRACE("Failed to read MemTotal from /proc/meminfo");
        return;
    }

    if (mem_free == 0) {
        TRACE("Failed to read MemFree from /proc/meminfo");
        return;
    }

    double mem_used_percentage = ((double)(mem_total - mem_free) / (double)mem_total) * 100.0;
    printf(" | Memory Used Percentage: %5.1f%%", mem_used_percentage);
}

/* Helper function to open the /proc/net/dev file */ 
FILE* open_proc_net_dev(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        TRACE("fopen() - Network");
    }
    return file;
}

/* Helper function to skip header lines in /proc/net/dev */
int skip_header_lines(FILE *file, int num_lines) {
    char buffer[LINE_BUFFER_SIZE];
    for (int i = 0; i < num_lines; ++i) {
        if (!fgets(buffer, sizeof(buffer), file)) {
            TRACE("fgets() - Network");
            return -1;
        }
    }
    return 0;
}

/* Helper function to parse a line and extract interface statistics */ 
int parse_interface_line(const char *line, char *iface, unsigned long *recv, unsigned long *send) {
    // Use the sscanf format string 
    if (sscanf(line, "%31s %lu %*u %*u %*u %*u %*u %*u %*u %lu", iface, recv, send) == 3) {
        return 1;
    }
    return 0;
}

/* Main function to retrieve and print network statistics for a given interface */ 
void network_stats(char *interface) {
    FILE *net_file = open_proc_net_dev(PROC_NET_DEV);
    if (!net_file) {
        return;
    }

    /* Skip the first two header lines */
    if (skip_header_lines(net_file, 2) != 0) {
        fclose(net_file);
        return;
    }

    char line[LINE_BUFFER_SIZE];
    char iface[IFACE_NAME_SIZE];
    unsigned long recv = 0, send = 0;
    int found = 0;

    /* Iterate through each line to find the matching interface */ 
    while (fgets(line, sizeof(line), net_file)) {
        if (parse_interface_line(line, iface, &recv, &send)) {
            if (strcmp(iface, interface) == 0) {
                printf(" | %s Receive: %lu bytes | %s Send: %lu bytes\n", interface, recv, interface, send);
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        printf("Interface %s not found.\n", interface);
    }

    fclose(net_file);
}

void disk_stats(const char *target_disk) {
    const char *PROC_DISK_STATS = "/proc/diskstats";
    FILE *file;
    char buffer[1024];

    /* Open the /proc/diskstats file */
    file = fopen(PROC_DISK_STATS, "r");
    if (!file) {
        perror("Error opening /proc/diskstats");
        return;
    }

    /* Read each line from the file */
    while (fgets(buffer, sizeof(buffer), file)) {
        char device_name[32];
        unsigned long read_time_ms, write_time_ms;

        /* Parse the line for the required fields */
        if (sscanf(buffer, "%*u %*u %s %*u %*u %*u %lu %*u %*u %*u %lu", 
                   device_name, &read_time_ms, &write_time_ms) != 3) {
            continue; /* Skip lines that don't match the format */
        }

        /* Check if the device matches the target disk */
        if (strcmp(device_name, target_disk) == 0) {
            printf(" | %s Read: %lu ms | %s Write: %lu ms\n", 
                   target_disk, read_time_ms, target_disk, write_time_ms);
            break; /* Exit the loop once the target disk is found */
        }
    }

    /* Close the file */
    fclose(file);
}

int main(int argc, char *argv[]) {
    const char *const PROC_STAT = "/proc/stat";

    char line[1024];
    FILE *file;

    UNUSED(argc);
    UNUSED(argv);

    if (SIG_ERR == signal(SIGINT, _signal_)) {
        TRACE("signal()");
        return -1;
    }

    while (!done) {
        if (!(file = fopen(PROC_STAT, "r"))) {
            TRACE("fopen() - CPU");
            return -1;
        }
        if (fgets(line, sizeof(line), file)) {
            printf("\rCPU Utilization: %5.1f%%", cpu_util(line));
            fflush(stdout);
        }
        fclose(file);

        memory_stats();
        network_stats("enp0s1:");
        disk_stats("sda");

        us_sleep(500000);
    }

    printf("\rDone!                                                                                            \n");
    return 0;
}
