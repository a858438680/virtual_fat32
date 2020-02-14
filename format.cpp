#include <stdexcept>
#include <string>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include "dev_io.h"

option long_options[] = {
    {"block", required_argument, NULL, 'b'},
    {"help", no_argument, NULL, 'h'},
};

void print_help(char *argv0)
{
    printf(
        "Usage: %s [OPTION]... NAME FILESIZE\n"
        "Create a disk in FAT32 format, having size of FILESIZE.\n"
        "Arguments:\n"
        "  -b, --block                block size in bytes, default 512\n"
        "                             can be 512, 1024, 2048 or 4096\n"
        "  -h, --help                 show help messages\n"
        "The FILESIZE argument is an integer and a unit.\n"
        "Units are MiB, GiB (powers of 1024) or KB,MB,... (powers of 1000).\n",
        argv0);
}

uint64_t get_size(const char *str)
{
    uint64_t res = atoi(str);
    if (!res)
    {
        auto msg = std::string(str) + " does not contain a valid number";
        throw std::runtime_error(msg.c_str());
    }
    auto head = str;
    while (isdigit(*head))
    {
        ++head;
    }
    auto len = strlen(str);
    auto unit_len = len - (head - str);
    if (unit_len == 2)
    {
        if (strcmp(head, "MB") == 0)
        {
            res *= 1000000;
        }
        else if (strcmp(head, "GB") == 0)
        {
            res *= 1000000000;
        }
        else
        {
            auto msg = std::string(head) + " is not a valid unit";
            throw std::runtime_error(msg.c_str());
        }
    }
    else if (unit_len == 3)
    {
        if (strcmp(head, "MiB") == 0)
        {
            res *= 1024 * 1024;
        }
        else if (strcmp(head, "GiB") == 0)
        {
            res *= 1024 * 1024 * 1024;
        }
        else
        {
            auto msg = std::string(head) + " is not a valid unit";
            throw std::runtime_error(msg.c_str());
        }
    }
    else
    {
        auto msg = std::string(head) + " is not a valid unit";
        throw std::runtime_error(msg.c_str());
    }
    return res;
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    extern char *optarg;
    extern int optind, opterr, optopt;

    opterr = 0;
    uint16_t block_size = 512;
    int invalid_opt = 0;

    while (true)
    {
        if (invalid_opt)
            break;

        int option_index;
        int c = getopt_long(argc, argv, "b:h", long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 'b':
            block_size = atoi(optarg);
            break;

        case 'h':
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
            break;

        case '?':
            invalid_opt = 1;
            break;
        }
    }

    if (invalid_opt)
    {
        fprintf(stderr, "invalid argument %c\n", (char)optopt);
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc - optind < 2)
    {
        fprintf(stderr, "no enough arguments\n");
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (block_size != 512 && block_size != 1024 && block_size != 2048 && block_size != 4096)
    {
        fprintf(stderr, "%hd is not a valid block size\nblock size must be one of 512, 1024, 2048 and 4096\n");
        exit(EXIT_FAILURE);
    }

    char *name = argv[optind];
    char *size_str = argv[optind + 1];
    uint64_t size;
    try
    {
        size = get_size(size_str);
    }
    catch (std::runtime_error &e)
    {
        fprintf(stderr, "invalid size, %s\n", e.what());
        exit(EXIT_FAILURE);
    }
    uint32_t tot_block = (size - 1 + block_size) / block_size;

    try
    {
        dev_io::dev_t(name, tot_block, block_size);
    }
    catch (std::exception &e)
    {
        fprintf(stderr, "create disk image failed, %s\n", e.what());
        exit(EXIT_FAILURE);
    }

    return 0;
}