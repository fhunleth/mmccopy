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

#include "config.h"

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

#define ONE_KiB  (1024ULL)
#define ONE_MiB  (1024 * ONE_KiB)
#define ONE_GiB  (1024 * ONE_MiB)

#define COPY_BUFFER_SIZE ONE_MiB

struct suffix_multiplier
{
    const char *suffix;
    size_t multiple;
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

// Progress and verbosity global variables
bool numeric_progress = false;
bool quiet = false;

void print_version()
{
    fprintf(stderr, "%s version %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

void print_usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options] [path]\n", argv0);
    fprintf(stderr, "  -d <Device file for the memory card>\n");
    fprintf(stderr, "  -n   Report numeric progress\n");
    fprintf(stderr, "  -o <Offset from the beginning of the memory card>\n");
    fprintf(stderr, "  -p   Report progress (default)\n");
    fprintf(stderr, "  -q   Quiet\n");
    fprintf(stderr, "  -r   Read from the memory card\n");
    fprintf(stderr, "  -s <Amount to read/write>\n");
    fprintf(stderr, "  -v   Print out the version and exit\n");
    fprintf(stderr, "  -w   Write to the memory card (default)\n");
    fprintf(stderr, "  -y   Accept automatically found memory card\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "The [path] specifies the location of the image to copy to or from\n");
    fprintf(stderr, "the memory card. If it is unspecified or '-', the image will either\n");
    fprintf(stderr, "be read from stdin (-w) or written to stdout (-r).\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Write the file sdcard.img to an automatically detected SD Card:\n");
    fprintf(stderr, "  %s sdcard.img\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "Read the master boot record (512 bytes @ offset 0) from /dev/sdc:\n");
    fprintf(stderr, "  %s -r -s 512 -o 0 -d /dev/sdc mbr.img\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "Offset and size may be specified with the following suffixes:\n");
    for (size_t i = 0; i < NUM_ELEMENTS(suffix_multipliers); i++)
        fprintf(stderr, "  %3s  %d\n", suffix_multipliers[i].suffix, (int) suffix_multipliers[i].multiple);
}

size_t parse_size(const char *str)
{
    char *suffix;
    size_t value = strtoul(str, &suffix, 10);

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

size_t device_size(const char *devpath)
{
    int fd = open(devpath, O_RDONLY);
    if (fd < 0)
        return 0;

    off_t len = lseek(fd, 0, SEEK_END);
    close(fd);

    return len < 0 ? 0 : len;
}

bool is_mmc_device(const char *devpath)
{
    // Check 1: Path exists and can read length
    size_t len = device_size(devpath);
    if (len == 0)
        return false;

    // Check 2: Capacity larger than 32 GiB -> false
    if (len > (32 * ONE_GiB))
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
        sprintf(out, "%d KiB", (int) (amount / ONE_KiB));
    else
        sprintf(out, "%d bytes", (int) amount);
}

void report_progress(size_t written, size_t total)
{
    if (quiet)
	return;

    if (numeric_progress) {
        // If numeric, write the percentage if we can figure it out.
        printf("%d\n", calculate_progress(written, total));
    } else {
        // If this is for a human, then print the percent complete
        // if we can calculate it or the bytes written.
        if (total > 0)
            printf("\r%d%%", calculate_progress(written, total));
        else {
            char sizestr[32];
            pretty_size(written, sizestr);
            printf("\r%s     ", sizestr);
        }
        fflush(stdout);
    }
}

void copy(int from_fd, int to_fd, size_t total_to_copy)
{
    report_progress(0, total_to_copy);

    char *buffer = malloc(COPY_BUFFER_SIZE);
    off_t total_written = 0;
    while (total_to_copy == 0 || total_written < total_to_copy) {
        size_t amount_to_read = COPY_BUFFER_SIZE;
        if (total_to_copy != 0 && total_to_copy < amount_to_read)
            amount_to_read = total_to_copy;

        ssize_t amount_read = read(from_fd, buffer, amount_to_read);
        if (amount_read < 0)
            err(EXIT_FAILURE, "read");

        if (amount_read == 0)
            break;

        char *ptr = buffer;
        do {
            ssize_t amount_written = write(to_fd, ptr, amount_read);
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

	report_progress(total_written, total_to_copy);
    }
    free(buffer);

    // Print a linefeed at the end so that the final progress report has
    // a new line after it. Numeric progress already prints linefeeds, so
    // don't add another on those.
    if (!quiet && !numeric_progress)
	printf("\n");
}

int main(int argc, char *argv[])
{
    const char *mmc_device = 0;
    const char *data_pathname = "-";
    size_t total_to_copy = 0;
    off_t seek_offset = 0;
    bool accept_found_device = false;
    bool read_from_mmc = false;

    // Memory cards are too big to bother with systems
    // that don't support large file sizes any more.
    if (sizeof(off_t) != 8)
        errx(EXIT_FAILURE, "recompile with largefile support");

    int opt;
    while ((opt = getopt(argc, argv, "d:s:o:npqrvwy")) != -1) {
        switch (opt) {
        case 'd':
            mmc_device = optarg;
            break;
        case 's':
            total_to_copy = parse_size(optarg);
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
        case 'r':
            read_from_mmc = true;
            break;
        case 'w':
	    read_from_mmc = false;
	    break;
        case 'y':
            accept_found_device = true;
            break;
        case 'v':
            print_version();
            exit(EXIT_SUCCESS);
            break;
        default: /* '?' */
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (quiet && numeric_progress)
        errx(EXIT_FAILURE, "pick either -n or -q, but not both.");

    if (optind < argc)
        data_pathname = argv[optind];

    if (read_from_mmc && total_to_copy == 0)
	errx(EXIT_FAILURE, "Specify the amount to copy (-s) when reading from memory card.");

    if (!mmc_device) {
        mmc_device = find_mmc_device();
        if (!mmc_device) {
	    if (getuid() != 0)
		errx(EXIT_FAILURE, "Memory card couldn't be found automatically.\nTry running as root or specify -? for help");
	    else
		errx(EXIT_FAILURE, "No memory cards found.");
	}

        if (!accept_found_device) {
            if (strcmp(data_pathname, "-") == 0)
                errx(EXIT_FAILURE, "Cannot confirm use of %s when using stdin/stdout.\nRerun with -y if location is correct.", mmc_device);

            char sizestr[16];
            pretty_size(device_size(mmc_device), sizestr);
            fprintf(stderr, "Use %s memory card found at %s? [y/N] ", sizestr, mmc_device);
            int response = fgetc(stdin);
            if (response != 'y' && response != 'Y')
                errx(EXIT_FAILURE, "aborted");
        }
    }

    int data_fd;
    if (strcmp(data_pathname, "-") != 0) {
	if (read_from_mmc)
	    data_fd = open(data_pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	else
	    data_fd = open(data_pathname, O_RDONLY);
        if (data_fd < 0)
            err(EXIT_FAILURE, "%s", data_pathname);

	// If writing to the MMC, cap the number of bytes to write to the file size.
	if (!read_from_mmc) {
	    struct stat st;
	    if (fstat(data_fd, &st))
		err(EXIT_FAILURE, "fstat");

	    if (total_to_copy == 0 ||
                st.st_size < total_to_copy)
		total_to_copy = st.st_size;
	}
    } else {
	// Reading from stdin or stdout.
	if (read_from_mmc) {
	    data_fd = STDOUT_FILENO;

	    // Force quiet to true so that progress reports don't stomp on
	    // the data.
	    quiet = true;
	} else
	    data_fd = STDIN_FILENO;
    }

    if (numeric_progress &&
            total_to_copy == 0)
        errx(EXIT_FAILURE, "Specify input size to report numeric progress");

    // Unmount everything so that our read and writes to the device are
    // unaffected by file system caches or other concurrent activity.
    umount_all_on_dev(mmc_device);

    int mmc_fd = open(mmc_device, read_from_mmc ? O_RDONLY : (O_WRONLY | O_SYNC));
    if (mmc_fd < 0)
        err(EXIT_FAILURE, "%s", mmc_device);

    if (lseek(mmc_fd, seek_offset, SEEK_SET) == (off_t) -1)
        err(EXIT_FAILURE, "lseek");

    if (read_from_mmc)
	copy(mmc_fd, data_fd, total_to_copy);
    else
	copy(data_fd, mmc_fd, total_to_copy);

    close(mmc_fd);
    if (data_fd != STDOUT_FILENO && data_fd != STDIN_FILENO)
        close(data_fd);

    exit(EXIT_SUCCESS);
}
