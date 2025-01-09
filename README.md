# Netmap: a framework for fast packet I/O

## Introduction

Netmap is a framework for high-speed packet I/O from userspace. This repo is forked from the [original one here](https://github.com/luigirizzo/netmap.git).
The original Netmap framework is designed for Linux 4.4 or earlier. Many of its driver's native support hasn't been updated, leaving users with only the generic emulated adapter for use.
Being stuck for the same reason with my Realtek network driver, I decided to update the existing codebase for native support on Linux 6.11, which it once had for Linux 2.4 and Linux 3.2
```
lspci | grep net
04:00.0 Ethernet controller: Realtek Semiconductor Co., Ltd. RTL8111/8168/8211/8411 PCI Express Gigabit Ethernet Controller (rev 16)
```
This readme will also point out specific areas of code which I changed and detailed steps for installing and updating your in-tree kernel drivers. The process for out-of-tree drivers is roughly the same.

The driver code responsible for my NIC is `r8169`.
```bash
lsmod
Module                  Size  Used by
...			...   ...
r8169                 126976  0
```

## Install Prerequisites
Install all the required dependencies.
```bash
sudo apt-get update # update your local package index
sudo apt install build-essential # install compiling tools
sudo apt-get install -y git # install git tools
sudo apt-get install -y linux-headers-$(uname -r) # install your linux headers
sudo apt-get install linux-source # install your linux source
sudo apt-get install net-tools # install net tools for ifconfig command
```
After you are done with these steps, your `/usr/src` folder will look something like this,
```bash
cd /usr/src/
ls
linux-headers-6.11.0-13  linux-headers-6.11.0-13-generic  linux-source-6.11.0  linux-source-6.11.0.tar.bz2
```
If `linux-source-6.11.0` is not present and you have `linux-source-6.11.0.tar.bz2` instead. Then unzip it
```
tar -xvf linux-source-6.11.0.tar.bz2
```
Note: Your Linux version can differ from mine.

## Updating Native Driver Support
Although this repo contains the updates for Netmap native support on `r8169` for Linux 6.11.13. The steps here are important in case you want to update your driver.

Clone the original Netmap GitHub repo in a suitable location.
```bash
git clone https://github.com/luigirizzo/netmap.git
cd netmap/LINUX
```
Now, the first step is to configure Netmap for your kernel and machine which is done using `./configure` with relevant flags. You can try running it and observe the errors and warnings. If everything works and you don't get any warnings or errors about disabling your particular driver. Then you are good to go :)

For others, stick to this guide for solutions. The files which are of interest for `r8169` are:
- `configure`: Contains the configuration script which creates all the Makefiles, finds your driver code, sets up everything and runs tests to adapt your environment, etc.
- `final-patches/`: Contains all the patch files required for modifying the native drivers with Netmap support.
- `if_re_netmap_linux.h`: Contains functions which act as a bridge between Netmap interface and NIC driver (in our case Realtek `r8169`).

First observe the latest patch for this driver available in `final-patches/` viz `vanilla--r8169.c--20626--30400`. Since, this is a patch applied to the in-tree kernel driver find the code for this driver inside your Linux kernel. Mine happens to be here,
```bash
ls /usr/src/linux-source-6.11.0/drivers/net/ethernet/realtek/
8139cp.c  8139too.c  Kconfig  Makefile  atp.c  atp.h  r8169.h  r8169_firmware.c  r8169_firmware.h  r8169_leds.c  r8169_main.c  r8169_phy_config.c
```
In the earlier versions of the kernel it was a single `/drivers/net/r8169.c` file ([Check here](https://github.com/torvalds/linux/tree/v2.6.39/drivers/net)). But now it has been renamed and refactored into `realtek` folder with further classification of `net` into `ethernet` folder. Copy the `realtek` folder into another empty folder (say, `r8169_patch`) at a suitable location
```bash
mkdir r8169_patch
cd r8169_patch
cp /usr/src/linux-source-6.11.0/drivers/net/ethernet/realtek .
git init
git add .
git commit -m "initial commit"
```
Now, comparing with `final-patches/vanilla--r8169.c--20626--30400` make the necessary changes to `r8169_main.c`. A nice description of what each change intends to do is presented [here](https://github.com/luigirizzo/netmap/issues/264#issuecomment-269612951).
After changes are done (which should be pretty quick), produce the diff file using
```
git diff > vanilla--realtek--60000--99999
```
I choose the version numbers to be `60000` which means `6.00.00` and `99999` which means `9.99.99` since, my kernel version is `6.11.13` which lies in between these versions. You can choose these accordingly as per your convenience or plans to keep generating more patches in the future.

Note: I have named the driver `realtek` instead of `r8169.c` because earlier it was just a single file, but now it is a folder and we gotta deal with the patch file for a folder as a whole.

Now, after this make the relevant changes to `if_re_netmap_linux.h` file, which we import near the start of our patched `r8169_main.c`. In my case, I just fixed 2 functions `RTL_W8` and `nm_kr_rxspace`. If you keep this file unchanged it is fine. When you run `make` command it will inform you about compilation errors and you can fix them.

Now, I made relevant changes to the `configure` script to change the driver name from `r8169.c` to `realtek` and some other changes incorporated due to this name change as well as the change from a single file to a folder. These changes are marked with `rishidarkdevil` comments in the `configure` script. `Ctrl + F` all the `rishidarkdevil` to understand the changes made to this file. That's it! 

## Install Netmap with Native Driver Support
Now run the following commands to build and install Netmap on your machine.
```bash
./configure --kernel-sources=/usr/src/linux-source-6.11.0 --no-ext-drivers --drivers=realtek --enable-vale --enable-pipe --enable-monitor --enable-ptnetmap --enable-sink
**********************************  NOTE   **********************************
*** Running some preliminary tests to customize the build environment.
*****************************************************************************
**********************************  NOTE   **********************************
*** Now running compile tests to adapt the code to your
*** kernel version. Please wait.
*****************************************************************************
kernel directory            /lib/modules/6.11.0-13-generic/build
                            [/usr/src/linux-headers-6.11.0-13-generic]
kernel sources              /usr/src/linux-source-6.11.0
linux version               60b00  [6.11]
module file                 netmap.ko

subsystems                  null sink ptnetmap generic monitor pipe vale 
apps                        dedup vale-ctl nmreplay tlem lb bridge pkt-gen 
native drivers              realtek 

Contents of the drivers.mak file:
realtek@conf := CONFIG_R8169
realtek@src := cp -Rp /usr/src/linux-source-6.11.0/drivers/net/ethernet/realtek realtek
realtek@patch := patches/vanilla--realtek--60000--99999

make
... output not shown ...

make apps

sudo make install
```

## Load Netmap
Unload the modules running on your NIC.
```bash
sudo rmmod r8169
```
Load Netmap and the patched compiled module for the NIC driver.
```bash
sudo insmod ./netmap.ko
sudo insmod ./realtek/r8169.ko
```
Start the interface.
```bash
sudo ifconfig eth0 up # and same for others
```

## Test Netmap

Test `pkt-gen` transmit using Netmap by,
```
sudo ethtool -K enp4s0 tx off rx off gso off tso off gro off lro off
sudo ethtool -s enp4s0 autoneg off
sudo pkt-gen -i enp4s0 -f tx -w 2 -s <SOME SOURCE IP> -d <SOME DESTINATION IP>
```
Here, `enp4s0` is my ethernet network interface name you can get yours using `ip a` command.

If you find this useful, consider showing your love by starring this repo :)
