#!/bin/sh

# --- Stops the script if errors are encountered. ---
set -e
# ---

CPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null)
# FreeBSD and similar...
[ -z "$CPUS" ] && CPUS=$(getconf NPROCESSORS_ONLN)
# Solaris and similar...
[ -z "$CPUS" ] && CPUS=$(ksh93 -c 'getconf NPROCESSORS_ONLN')
# Give up...
[ -z "$CPUS" ] && CPUS=1
# ---

FPPVERSION="2.15"
CONFIGNAME="fppconfig"
COMMITHASH="d6800a124dbba118e297188900d07adfea661b87"
CONFIGLINK="https://github.com/Birdthulu/FPM-Installer/raw/master/config/$FPPVERSION-$CONFIGNAME.tar.gz"
GITCLONELINK="https://github.com/Birdthulu/Ishiiruka"
SdCardFileName="ProjectPlusSdCard215.tar.gz"
SdCardDlHash="0anckw4hrxlqn5i"
SdCardLink="http://www.mediafire.com/file/$SdCardDlHash/$SdCardFileName/file"
# FOLDERNAME="FasterProjectPlus-${FPPVERSION}"

# rm -r "$FOLDERNAME"

# --- enter folder, download and extract needed files
# echo ""
# mkdir "$FOLDERNAME" && cd "$FOLDERNAME"
# echo "Downloading config files..."
# curl -LO# $CONFIGLINK
# echo "Extracting config files..."
# tar -xzf "$FPPVERSION-$CONFIGNAME.tar.gz" --checkpoint-action='exec=printf "%d/410 records extracted.\r" $TAR_CHECKPOINT' --totals
# rm "$FPPVERSION-$CONFIGNAME.tar.gz"

# mv "Ishiiruka-$COMMITHASH" Ishiiruka
# cd Ishiiruka

cd build

echo "cmaking..."
if [ ! -z "${IN_NIX_SHELL++}" ]; then
	cmake .. -DLINUX_LOCAL_DEV=true -DGTK2_GLIBCONFIG_INCLUDE_DIR=${glib}/lib/glib-2.0/include -DGTK2_GDKCONFIG_INCLUDE_DIR=${gtk2}/lib/gtk-2.0/include -DGTK2_INCLUDE_DIRS=${gtk2}/lib/gtk-2.0 -DENABLE_LTO=True -DCMAKE_INSTALL_PREFIX=/usr
else
	cmake .. -DLINUX_LOCAL_DEV=true -DCMAKE_INSTALL_PREFIX=/usr
fi
echo "Compiling..."
make -j $CPUS -s
make install DESTDIR=AppDir

echo ""
mkdir "$FOLDERNAME" && cd "$FOLDERNAME"
echo "Downloading config files..."
curl -LO# $CONFIGLINK
echo "Extracting config files..."
tar -C "AppDir/usr/bin/" -xzf "$FPPVERSION-$CONFIGNAME.tar.gz" --checkpoint-action='exec=printf "%d/410 records extracted.\r" $TAR_CHECKPOINT' --totals
mv AppDir/usr/bin/Binaries/* ../ -f
rm "$FPPVERSION-$CONFIGNAME.tar.gz"

# --- Download the sd card tarball, extract it, move it to proper folder, and delete tarball
# credit to https://superuser.com/a/1517096 superuser.com user Zibri
echo "Downloading sd card"
url=$(curl -Lqs0 "$SdCardLink" | grep "href.*download.*media.*"| grep "$SdCardFileName" | cut -d '"' -f 2)

echo "Attempting with Axel";
if [ -x "$(command -v axel)" ]; then
	axel "$url" -a -n $CPUS; 
elif [ -x "$(command -v aria2c)" ]; then
	echo "Axel command failed, dependency may be missing, attempting with aria2c";
	aria2c -x$CPUS "$url"
else
 echo "Axel and Aria2c command failed, dependency may be missing, reverting to slowest way possible";
 wget "$url"
fi

echo "Extracting sd card"
tar -C  "AppDir/usr/bin/User/Wii/" -xzf "$SdCardFileName" --checkpoint-action='exec=printf "%d/12130 records extracted.\r" $TAR_CHECKPOINT' --totals
rm "$SdCardFileName"
# ---

./linuxdeploy-x86_64.AppImage --appdir AppDir/ --output appimage