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
    -o, --preserve-ownership       preserve ownership
    -p, --preserve-permissions     preserve permissions
    -P, --preserve-all             preserve all timestamp, ownership, and
                                   permission data
    -t, --preserve-timestamp       preserve timestamps
    -u <N>, --update-interval=<N>  set the progress update interval to every
                                   <N> seconds (default is 1 second)
    --no-progress                  do not show any progress during copy
                                   operations
    --no-report                    do not show completion report after copy
                                   operations are finished
    -h, --help                     print this text and exit
    -v, --version                  print version information and exit

Installing
----------
Some Windows code has been added but this program most likely will only run
Linux machines (developed on Ubuntu 12.04).

    make
    sudo make install
