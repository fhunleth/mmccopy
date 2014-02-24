# Overview

The `mmccopy` utility is an easier-to-use alternative to `dd` for
writing images to SDCards and MMC memory. It has the following
features:

  1. Write image data from `stdin` or a file directly to an offset on
  the device (this is similar to `dd` except that offsets are
  specified in bytes instead of blocks)

  2. Batch up writes into 1 MiB blocks by default to improve transfer
  rate. (`dd` defaults to 512 byte blocks without the `bs` argument)

  3. Automatically unmount partitions that are using the device. This
  prevents data corruption either due to latent writes from the
  mounted file systems or due to image writes being cached.

  4. Provide human and machine readable progress similar to using `pv`
  with `dd`, except with the improvement that the percentages track
  completed writes rather than initiated writes.

  5. Automatic detection of MMC and SDCards. This option queries the
  user before writing anything by default to avoid accidental
  overwrites.

Here's an example run:

    $ sudo mmccopy -p sdcard.img
    Use memory card found at /dev/sdc? [y/N] y
    100%
    $

# Building from source

Clone or download the source code and run the following:

    ./configure
    make
    make install

# Invoking

```
Usage: mmccopy [options] [path]
  -d <Device file for the memory card>
  -n   Report numeric progress
  -o <Offset from the beginning of the memory card>
  -p   Report progress (default)
  -q   Quiet
  -r   Read from the memory card
  -s <Amount to read/write>
  -v   Print out the version and exit
  -w   Write to the memory card (default)
  -y   Accept automatically found memory card

The [path] specifies the location of the image to copy to or from
the memory card. If it is unspecified or '-', the image will either
be read from stdin (-w) or written to stdout (-r).

Examples:

Write the file sdcard.img to an automatically detected SD Card:
  mmccopy sdcard.img

Read the master boot record (512 bytes @ offset 0) from /dev/sdc:
  mmccopy -r -s 512 -o 0 -d /dev/sdc mbr.img

Offset and size may be specified with the following suffixes:
    b  512
   kB  1000
    K  1024
  KiB  1024
   MB  1000000
    M  1048576
  MiB  1048576
   GB  1000000000
    G  1073741824
  GiB  1073741824
```
