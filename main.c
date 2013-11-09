#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#define NUM_ELEMENTS(X) (sizeof(X) / sizeof(X[0]))

#ifdef _LARGEFILE64_SOURCE
#define OFF_T off64_t
#else
#define OFF_T off_t
#endif

struct suffix_multiplier
{
    const char *suffix;
    OFF_T multiple;
};

struct suffix_multiplier suffix_multipliers[] = {
    {"b", 512},
    {"kB", 1000},
    {"K", 1024},
    {"MB", 1000 * 1000},
    {"M", 1024 * 1024},
    {"GB", 1000 * 1000 * 1000},
    {"GiB", 1024 * 1024 * 1024}
};

void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options] [filename]\n", argv0);
    fprintf(stderr, "  -d <Device file for the memory card>\n");
    fprintf(stderr, "  -s <Amount to write>\n");
    fprintf(stderr, "  -o <Offset from the beginning of the memory card>\n");
    fprintf(stderr, "  -n   Report numeric progress\n");
    fprintf(stderr, "  -p   Report progress\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Offset and size may be specified with the following suffixes:\n");
    size_t i;
    for (i = 0; i < NUM_ELEMENTS(suffix_multipliers); i++)
        fprintf(stderr, "  %3s  %d\n", suffix_multipliers[i].suffix, (int) suffix_multipliers[i].multiple);
}

OFF_T parse_size(const char *str)
{
    char *suffix;
    OFF_T value = strtoul(str, &suffix, 10);

    if (suffix == str)
        errx(EXIT_FAILURE, "Expecting number but got '%s'\n", str);

    if (*suffix == '\0')
        return value;

    size_t i;
    for (i = 0; i < NUM_ELEMENTS(suffix_multipliers); i++) {
        if (strcmp(suffix_multipliers[i].suffix, suffix) == 0)
            return value * suffix_multipliers[i].multiple;
    }

    errx(EXIT_FAILURE, "Unknown size multiplier '%s'\n", suffix);
    return 0;
}

int main(int argc, char *argv[])
{

    const char *mmc_device = 0;
    const char *source = "-";
    OFF_T amount_to_write = 0;
    OFF_T seek_offset = 0;
    bool numeric_progress = false;
    bool human_progress = false;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:o:np")) != -1) {
        switch (opt) {
        case 'd':
            mmc_device = optarg;
            break;
        case 's':
            amount_to_write = parse_size(optarg);
            break;
        case 'o':
            seek_offset = parse_size(optarg);
            break;
        case 'n':
            numeric_progress = true;
            break;
        case 'p':
            human_progress = true;
            break;
        default: /* '?' */
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (human_progress && numeric_progress)
        errx(EXIT_FAILURE, "Pick either -n or -p, but not both.\n");

    if (!mmc_device)
        errx(EXIT_FAILURE, "Must specify destination\n");

    if (optind < argc)
        source = argv[optind];

    int input_fd = 0;
    if (strcmp(source, "-") != 0) {
        input_fd = open(source, O_RDONLY);
        if (input_fd < 0)
            err(EXIT_FAILURE, "%s", source);

        struct stat st;
        if (fstat(input_fd, &st))
            err(EXIT_FAILURE, "fstat");

        if (amount_to_write == 0 ||
            st.st_size < amount_to_write)
            amount_to_write = st.st_size;
    }

    if ((numeric_progress || human_progress) &&
            amount_to_write == 0)
        errx(EXIT_FAILURE, "Specify input size to show progress");

    int output_fd = open(mmc_device, O_WRONLY);
    if (output_fd < 0)
        err(EXIT_FAILURE, "%s", mmc_device);

    if (
#ifdef _LARGEFILE64_SOURCE
            lseek64(output_fd, seek_offset, SEEK_SET) == (OFF_T) -1
#else
            lseek(output_fd, seek_offset, SEEK_SET) == (OFF_T) -1
#endif
        )
        err(EXIT_FAILURE, "seek");

#define BUFFER_SIZE (1024*1024)

    if (amount_to_write > 0) {
        if (numeric_progress)
            printf("0\n");
        if (human_progress) {
            printf("0%%");
            fflush(stdout);
        }
    }
    char *buffer = malloc(BUFFER_SIZE);
    OFF_T total_to_write = amount_to_write;
    for (;;) {
        size_t amount_to_read = BUFFER_SIZE;
        if (amount_to_write != 0 && amount_to_write < (OFF_T) amount_to_read)
            amount_to_read = amount_to_write;

        ssize_t amount_read = read(input_fd, buffer, amount_to_read);
        if (amount_read < 0)
            err(EXIT_FAILURE, "read");

        if (amount_read == 0)
            break;

        amount_to_write -= amount_read;

        char *ptr = buffer;
        do {
            ssize_t amount_written = write(output_fd, ptr, amount_read);
            if (amount_written < 0) {
                if (errno == EINTR)
                    continue;
                else
                    err(EXIT_FAILURE, "write");
            }

            amount_read -= amount_written;
            ptr += amount_written;
        } while (amount_read > 0);

        int progress = 100 * (total_to_write - amount_to_write) / total_to_write;
        if (numeric_progress)
            printf("%d\n", progress);
        if (human_progress) {
            printf("\b\b\b\b%d%%", progress);
            fflush(stdout);
        }
    }
    close(output_fd);
    if (input_fd != 0)
        close(input_fd);

    if (numeric_progress)
        printf("100");
    if (human_progress)
        printf("\b\b\b\b100%%\n");

    free(buffer);
    exit(EXIT_SUCCESS);
}
