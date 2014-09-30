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
    -n, --no-sound                 Do not play the notification sound when
                                   copy operations are completed.
                                   NOTE: This option only exists if the
                                         program was compiled with sound
                                         support!
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

Compiling and Installing
------------------------
Some Windows code has been added but this program will most likely only run on
Linux machines. It was written on Ubuntu 12.04.

To set up the build environment, navigate to the copy source directory and
run:

    ./autogen.sh

To configure:

    ./configure

If you would like to enable the handy notification sound upon completion of
all copy operations, make sure SDL and SDL_sound are installed on your
machine, and then run:

    ./configure --enable-sound

To compile:

    make

And finally to install to the default location of /usr/local/bin:

    sudo make install

If you would like to install to another location (such as /usr/bin in the
following example), use the 'prefix' variable:

    sudo make prefix=/usr install

If the '--enable-sound' option was used earlier when calling ./configure, the
audio file 'complete.oga' will be installed at: <prefix>/share/copy/sounds/complete.oga.
