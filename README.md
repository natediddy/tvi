tvi
===
TVI is a small tool written in C that can retrieve episode information about many TV shows from the command line.

All TV series data is obtained from [TV.com](http://www.tv.com/).

Usage
-----
    Usage: tvi [-adHilLnr] [-sN[,N,...]] [-eN[,N,...]] TITLE

Options
-------
    -e N, --episode=N     specify episode(s) N
                          more than 1 episode can be specified in a
                          comma-separated list: N1,N2,N3,...
    -s N, --season=N      specify season(s) N
                          more than 1 season can be specified in a
                          comma-separated list: N1,N2,N3,...
    -a, --air             print air date for each episode
    -d, --description     print description for each episode
    -H, --highest-rated   print highest rated episode of series
    -l, --last            print most recently aired episode
    -L, --lowest-rated    print lowest rated episode of series
    -n, --next            print the next upcoming episode scheduled to air
    -r, --rating          print rating for each episode
    -i, --info            print general info about TITLE
    -h, --help            print this text and exit
    -v, --version         print version information and exit

No more than 1 TITLE argument may be provided.

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
