# prj-tg-https

A small C++ library for downloading files over HTTPS on Linux.

**Note:** use it for small files only. The whole download is kept in memory
(no file cache), so large files need a lot of RAM.

## Requirements

On Ubuntu

```sh
sudo apt install build-essential pkg-config libssl-dev
```

## Building and installing

```sh
cd lib
make
sudo make install
```
