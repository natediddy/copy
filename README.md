copy
====
This is a small command line program that basically just copies files and
directories from one location to another. It is similar to the standard `cp'
command except that it provides progress updates during copy operations (but
admittedly has a lot less options and probably a lot more security
vulnerabilities compared to the former).

Usage
-----
    copy [OPTION...] SOURCE... DESTINATION

Options
-------
    -o, --preserve-ownership       Preserve ownership.
    -p, --preserve-permissions     Preserve permissions.
    -P, --preserve-all             Preserve all timestamp, ownership, and
                                   permission data.
    -t, --preserve-timestamp       Preserve timestamps.
    -u <N>, --update-interval=<N>  Set the progress update interval to every
                                   <N> seconds. The default is 0.5 seconds.
    -V, --verify                   Perform a MD5 checksum verification on
                                   DESTINATION files to ensure they match up
                                   with their corresponding SOURCE file.
                                   Note that this will take quite a bit more
                                   time to complete.
    --no-progress                  Do not show any progress during copy
                                   operations.
    --no-report                    Do not show completion report after copy
                                   operations are finished.
    -h, --help                     Print this text and exit.
    -v, --version                  Print version information and exit.

Installing
----------
Some Windows code has been added but this program most likely will only run
Linux machines (developed on Ubuntu 12.04).

    make
    sudo make install
