#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath "$(dirname "$0")")
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p "${OUTDIR}"
OUTDIR=$(realpath "${OUTDIR}")

cd "${OUTDIR}"

if [ ! -d "${OUTDIR}/linux-stable" ]; then
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone "${KERNEL_REPO}" --depth 1 --single-branch --branch "${KERNEL_VERSION}"
fi

if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout "${KERNEL_VERSION}"

    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/"

echo "Creating the staging directory for the root filesystem"
cd "${OUTDIR}"

if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf "${OUTDIR}/rootfs"
fi

mkdir -p rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr/bin,usr/lib,usr/sbin,var}

cd "${OUTDIR}"

if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout "${BUSYBOX_VERSION}"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi

echo "Configuring BusyBox for static ARM64 build"

sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config

make clean

make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}

make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
    CONFIG_PREFIX="${OUTDIR}/rootfs" install

echo "Creating device nodes"

sudo mknod -m 666 "${OUTDIR}/rootfs/dev/null" c 1 3
sudo mknod -m 600 "${OUTDIR}/rootfs/dev/console" c 5 1
sudo mknod -m 666 "${OUTDIR}/rootfs/dev/tty" c 5 0

echo "Building writer application"

make -C "${FINDER_APP_DIR}" clean
make -C "${FINDER_APP_DIR}" CROSS_COMPILE=${CROSS_COMPILE}

echo "Copying application files"

cp "${FINDER_APP_DIR}/writer" "${OUTDIR}/rootfs/home/"
cp "${FINDER_APP_DIR}/finder.sh" "${OUTDIR}/rootfs/home/"
cp "${FINDER_APP_DIR}/finder-test.sh" "${OUTDIR}/rootfs/home/"
cp "${FINDER_APP_DIR}/autorun-qemu.sh" "${OUTDIR}/rootfs/home/"

mkdir -p "${OUTDIR}/rootfs/home/conf"

cp "${FINDER_APP_DIR}/conf/username.txt" "${OUTDIR}/rootfs/home/conf/"
cp "${FINDER_APP_DIR}/conf/assignment.txt" "${OUTDIR}/rootfs/home/conf/"

echo "Copying writer runtime libraries"

LIBC_PATH=$(${CROSS_COMPILE}gcc -print-file-name=libc.so.6)
LOADER_PATH=$(${CROSS_COMPILE}gcc -print-file-name=ld-linux-aarch64.so.1)

if [ ! -f "${LIBC_PATH}" ] || [ ! -f "${LOADER_PATH}" ]; then
    echo "Error: Could not locate ARM64 runtime libraries"
    exit 1
fi

cp -L "${LIBC_PATH}" "${OUTDIR}/rootfs/lib/"
cp -L "${LOADER_PATH}" "${OUTDIR}/rootfs/lib/"

echo "Creating initramfs"

sudo chown -R root:root "${OUTDIR}/rootfs"

cd "${OUTDIR}/rootfs"
sudo find . | sudo cpio -H newc -o --owner root:root | gzip -9 > "${OUTDIR}/initramfs.cpio.gz"

echo "Build completed successfully"
