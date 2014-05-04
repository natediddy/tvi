ep
==
EP is a small tool written in C that can retrieve episode information about many TV shows from the command line.

All TV series data is obtained from [TV.com](http://www.tv.com/).

Usage
-----
    Usage: ep [-adr] [-s<N>] [-e<N>] <TITLE>

Options
-------
    -e<N>, --episode=<N>  specify episode <N>
    -s<N>, --season=<N>   specify season <N>
    -a, --air             print air date for each episode
    -d, --description     print description for each episode
    -r, --rating          print rating for each episode
    -h, --help            print this text and exit
    -v, --version         print version information and exit

Building
--------
The external library [LibcURL](http://curl.haxx.se/download.html/) is required
in order to build ep. Also make sure the GNU Autotools are installed.

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
