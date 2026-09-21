# vpkg (Volkrig Package Manager)

A simple, static package manager with no dependencies beyond `tar`.

## build

```sh
./build.sh
```

## usage

```sh
vpkg -b dir package.vpkg  # build a package
vpkg -i package.vpkg      # install
vpkg -r name             # remove
vpkg -q name             # show package information
vpkg -l                  # list installed packages
```

A package is a tar archive containing the files to install and `.vpkg/info`:

```text
name=example
version=1.0
arch=x86_64
description=example of the package
```

Package metadata and file lists are stored in `/var/lib/vpkg`.