tvi
===
TVI is a small tool written in C that can retrieve episode information about many TV shows from the command line.

All TV series data is obtained from [TV.com](http://www.tv.com/).

Usage
-----
    Usage: tvi [-adHilLnr] [-c[NAME]] [-sN[,N,...]] [-eN[,N,...]] TITLE

Options
-------
    -eN, --episode=N          specify episode(s) N
                              For more than one episode, use a
                              comma-separated list (e.g. "1,2,3").
    -sN, --season=N           specify season(s) N
                              For more than one season, use a
                              comma-separated list (e.g. "1,2,3").
    -a, --air                 print the air date for each episode
    -cNAME, --cast=NAME       print cast and crew members
                              If NAME is given, and it matches a cast
                              member's name, their respective role is
                              printed. On the other hand if NAME matches
                              a cast member's role, their respective
                              name is printed. If NAME is not given, all
                              cast and crew members are printed.
    -d, --description         print description for each episode
    -H, --highest-rated       print highest rated episode of series
    -l, --last                print most recently aired episode
    -L, --lowest-rated        print lowest rated episode of series
    -n, --next                print next episode scheduled to air
    -N, --no-progress         do not display any progress while
                              downloading data (useful for writing
                              output to a file)
    -r, --rating              print rating for each episode
    -h, --help                print this text and exit
    -v, --version             print version information and exit

Only one TITLE can be provided at a time.

Building
--------
The external library [LibcURL](http://curl.haxx.se/download.html/) is required
in order to build tvi. Also make sure the GNU Autotools are installed.

On a Linux system, simply run:

    ./autogen.sh
    ./configure
    make

Installing
----------
After successfully building on a Linux system, run:

    sudo make install

Contact
-------
Send questions or bug reports to sforbes41[at]gmail[dot]com.
