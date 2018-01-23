# Build EMANE

## Ubuntu 14.04

### Build emane

    cd emane
    ./autogen.sh && ./configure && make deb

### Install debs

    cd .debbuild
    sudo dpkg -i *.deb