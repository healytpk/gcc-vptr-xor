#!/bin/sh
export CONFIG_SHELL=/bin/sh
echo ===================================================================
echo   Installing pre-requisites
echo ===================================================================
sudo apt update
sudo apt install -y build-essential git make gawk flex bison libgmp-dev libmpfr-dev libmpc-dev python3 binutils perl libisl-dev libzstd-dev tar gzip bzip2 meld geany
./contrib/download_prerequisites
cd ..
if [ ! -d "gcc-thomas-healy_build" ]; then
  echo ===================================================================
  echo   Creating build directory and configuring build:
  echo      configure --prefix=`realpath --no-symlinks "$PWD/gcc-thomas-healy"`/ --disable-multilib --enable-languages=c,c++
  echo ===================================================================
  mkdir -p gcc-thomas-healy_build
  cd gcc-thomas-healy_build
  ../gcc-thomas-healy_source/configure --prefix=`realpath --no-symlinks "$PWD/gcc-thomas-healy"`/ --disable-multilib --enable-languages=c,c++
fi
echo ===================================================================
echo   Configuring git at the commandline with username and password
echo ===================================================================
git config --global user.email "thomaspkhealy@yahoo.com"
git config --global user.name "Thomas PK Healy"
echo ===================================================================
echo   Setting up bikeshed
echo ===================================================================
sudo apt-get install -y python3 python3-venv pipx
pipx ensurepath
. "$HOME/.profile"
pipx install bikeshed
bikeshed update
echo ===================================================================
echo   ALL DONE
echo ===================================================================
