/* The MIT License (MIT)
 *
 * Copyright (c) 2013 Frank Hunleth
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_ELEMENTS(X) (sizeof(X) / sizeof(X[0]))

#define ONE_GiB  (1024 * 1024 * 1024)
#define ONE_MiB  (1024 * 1024)
#define ONE_KiB  (1024)

#define COPY_BUFFER_SIZE ONE_MiB

struct suffix_multiplier
{
    const char *suffix;
    off_t multiple;
};

struct suffix_multiplier suffix_multipliers[] = {
    {"b", 512},
    {"kB", 1000},
    {"K", ONE_KiB},
    {"KiB", ONE_KiB},
    {"MB", 1000 * 1000},
    {"M", ONE_MiB},
    {"MiB", ONE_MiB},
    {"GB", 1000 * 1000 * 1000},
    {"G", ONE_GiB},
    {"GiB", ONE_GiB}
};

void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options] [inputpath]\n", argv0);
    fprintf(stderr, "  -d <Device file for the memory card>\n");
    fprintf(stderr, "  -s <Amount to write>\n");
    fprintf(stderr, "  -o <Offset from the beginning of the memory card>\n");
    fprintf(stderr, "  -n   Report numeric progress\n");
    fprintf(stderr, "  -p   Report progress\n");
    fprintf(stderr, "  -q   Quiet\n");
    fprintf(stderr, "  -y   Accept automatically found memory card\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "The inputpath specifies the location of the image to write to\n");
    fprintf(stderr, "the memory card. If it is unspecified or '-', the image will be\n");
    fprintf(stderr, "read from stdin.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Offset and size may be specified with the following suffixes:\n");
    for (size_t i = 0; i < NUM_ELEMENTS(suffix_multipliers); i++)
        fprintf(stderr, "  %3s  %d\n", suffix_multipliers[i].suffix, (int) suffix_multipliers[i].multiple);
}

off_t parse_size(const char *str)
{
    char *suffix;
    off_t value = strtoul(str, &suffix, 10);

    if (suffix == str)
        errx(EXIT_FAILURE, "Expecting number but got '%s'", str);

    if (*suffix == '\0')
        return value;

    for (size_t i = 0; i < NUM_ELEMENTS(suffix_multipliers); i++) {
        if (strcmp(suffix_multipliers[i].suffix, suffix) == 0)
            return value * suffix_multipliers[i].multiple;
    }

    errx(EXIT_FAILURE, "Unknown size multiplier '%s'", suffix);
    return 0;
}

void umount_all_on_dev(const char *mmc_device)
{
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp)
        err(EXIT_FAILURE, "/proc/mounts");

    char *todo[64] = {0};
    int todo_ix = 0;

    while (!feof(fp)) {
        char line[256] = {0};
        if (!fgets(line, sizeof(line), fp))
            break;

        char devname[64];
        char mountpoint[256];
        if (sscanf(line, "%s %s", devname, mountpoint) != 2)
            continue;

        if (strstr(devname, mmc_device) == devname) {
            // mmc_device is a prefix of this device, i.e. mmc_device is /dev/sdc
            // and /dev/sdc1 is mounted.

            if (todo_ix == NUM_ELEMENTS(todo))
                errx(EXIT_FAILURE, "Device mounted too many times");

            todo[todo_ix++] = strdup(mountpoint);
        }
    }
    fclose(fp);

    for (int i = 0; i < todo_ix; i++) {
        if (umount(todo[i]) < 0)
            err(EXIT_FAILURE, "umount %s", todo[i]);

        free(todo[i]);
    }
}

bool is_mmc_device(const char *devpath)
{
    // Check 1: Doesn't exist -> false
    int fd = open(devpath, O_RDONLY);
    if (fd < 0)
        return false;

    // Check 2: lseek fails -> false
    off_t len = lseek(fd, 0, SEEK_END);
    close(fd);
    if (len < 0)
        return false;

    // Check 3: Capacity larger than 16 GiB -> false
    if (len > 17179869184LL)
        return false;

    // Certainly there are more checks that we can do
    // to avoid false memory card detects...

    return true;
}

char *find_mmc_device()
{
    char *possible[64] = {0};
    size_t possible_ix = 0;

    // Scan memory cards connected via USB. These are /dev/sd_ devices.
    // NOTE: Don't scan /dev/sda, since I don't think this is ever right
    // for any use case.
    for (char c = 'b'; c != 'z'; c++) {
        char devpath[64];
        sprintf(devpath, "/dev/sd%c", c);

        if (is_mmc_device(devpath) && possible_ix < NUM_ELEMENTS(possible))
            possible[possible_ix++] = strdup(devpath);
    }

    // Scan the mmcblk devices
    for (int i = 0; i < 16; i++) {
        char devpath[64];
        sprintf(devpath, "/dev/mmcblk%d", i);

        if (is_mmc_device(devpath) && possible_ix < NUM_ELEMENTS(possible))
            possible[possible_ix++] = strdup(devpath);
    }

    if (possible_ix == 0)
        return 0;
    else if (possible_ix == 1)
        return possible[0];
    else {
        fprintf(stderr, "Too many possible memory cards found: \n");
        for (size_t i = 0; i < possible_ix; i++)
            fprintf(stderr, "  %s\n", possible[i]);
        fprintf(stderr, "Pick one and specify it explicitly on the commandline.\n");
        exit(EXIT_FAILURE);
    }
}

int calculate_progress(size_t written, size_t total)
{
    if (total > 0)
	return 100 * written / total;
    else
        return 0;
}

void pretty_size(size_t amount, char *out)
{
    if (amount >= ONE_GiB)
        sprintf(out, "%.2f GiB", ((double) amount) / ONE_GiB);
    else if (amount >= ONE_MiB)
        sprintf(out, "%.2f MiB", ((double) amount) / ONE_MiB);
    else if (amount >= ONE_KiB)
        sprintf(out, "%d KiB", ((int) amount / ONE_KiB));
    else
        sprintf(out, "%d bytes", (int) amount);
}

void report_progress(size_t written, size_t total, bool numeric)
{
    if (numeric) {
        // If numeric, write the percentage if we can figure it out.
	printf("%d\n", calculate_progress(written, total));
    } else {
        // If this is for a human, then print the percent complete
        // if we can calculate it or the bytes written.
        if (total > 0)
	    printf("\r%d%%", calculate_progress(written, total));
        else {
            char buffer[32];
            pretty_size(written, buffer);
            printf("\r%s     ", buffer);
        }
        fflush(stdout);
    }
}

int main(int argc, char *argv[])
{

    const char *mmc_device = 0;
    const char *source = "-";
    size_t total_to_write = 0;
    off_t seek_offset = 0;
    bool numeric_progress = false;
    bool accept_found_device = false;
    bool quiet = false;

    // Memory cards are too big to bother with systems
    // that don't support large file sizes any more.
    if (sizeof(off_t) != 8)
        errx(EXIT_FAILURE, "recompile with largefile support");

    int opt;
    while ((opt = getopt(argc, argv, "d:s:o:npqy")) != -1) {
        switch (opt) {
        case 'd':
            mmc_device = optarg;
            break;
        case 's':
            total_to_write = parse_size(optarg);
            break;
        case 'o':
            seek_offset = parse_size(optarg);
            break;
        case 'n':
            numeric_progress = true;
            break;
        case 'p':
            // This is now the default. Keep parameter around since I wrote
            // some docs that include it.
            break;
        case 'q':
            quiet = true;
            break;
        case 'y':
            accept_found_device = true;
            break;
        default: /* '?' */
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (quiet && numeric_progress)
        errx(EXIT_FAILURE, "pick either -n or -q, but not both.");

    if (optind < argc)
        source = argv[optind];

    if (!mmc_device) {
        mmc_device = find_mmc_device();
        if (!mmc_device)
            errx(EXIT_FAILURE, "memory card couldn't be found automatically (permissions?)");

        if (!accept_found_device) {
            if (strcmp(source, "-") == 0)
                errx(EXIT_FAILURE, "Cannot confirm %s when writing data from stdin. Rerun with -y.", mmc_device);

            fprintf(stderr, "Use memory card found at %s? [y/N] ", mmc_device);
            int response = fgetc(stdin);
            if (response != 'y' && response != 'Y')
                errx(EXIT_FAILURE, "aborted");
        }
    }

    int input_fd = 0;
    if (strcmp(source, "-") != 0) {
        input_fd = open(source, O_RDONLY);
        if (input_fd < 0)
            err(EXIT_FAILURE, "%s", source);

        struct stat st;
        if (fstat(input_fd, &st))
            err(EXIT_FAILURE, "fstat");

        if (total_to_write == 0 ||
            st.st_size < total_to_write)
            total_to_write = st.st_size;
    }

    if (numeric_progress &&
            total_to_write == 0)
        errx(EXIT_FAILURE, "Specify input size to report numeric progress");

    // Don't access the device if someone is using it.
    umount_all_on_dev(mmc_device);

    int output_fd = open(mmc_device, O_WRONLY | O_SYNC);
    if (output_fd < 0)
        err(EXIT_FAILURE, "%s", mmc_device);

    if (lseek(output_fd, seek_offset, SEEK_SET) == (off_t) -1)
        err(EXIT_FAILURE, "lseek");

    if (!quiet)
        report_progress(0, total_to_write, numeric_progress);

    char *buffer = malloc(COPY_BUFFER_SIZE);
    off_t total_written = 0;
    while (total_to_write == 0 || total_written < total_to_write) {
        size_t amount_to_read = COPY_BUFFER_SIZE;
        if (total_to_write != 0 && total_to_write < amount_to_read)
            amount_to_read = total_to_write;

        ssize_t amount_read = read(input_fd, buffer, amount_to_read);
        if (amount_read < 0)
            err(EXIT_FAILURE, "read");

        if (amount_read == 0)
            break;

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
	    total_written += amount_written;
        } while (amount_read > 0);

        if (!quiet)
            report_progress(total_written, total_to_write, numeric_progress);
    }
    close(output_fd);
    if (input_fd != 0)
        close(input_fd);

    if (!quiet)
        report_progress(total_written, total_to_write, numeric_progress);
    printf("\n");

    free(buffer);
    exit(EXIT_SUCCESS);
}
