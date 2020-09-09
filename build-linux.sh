#!/bin/sh

# --- Stops the script if errors are encountered. ---
set -e
# ---

function download_file {
	url = $1
	output = $2
	echo "Attempting with Axel";
	if [ -x "$(command -v axel)" ]; then
		axel "$url" -a -n $CPUS --output $output; 
	elif [ -x "$(command -v aria2c)" ]; then
		echo "Axel command failed, dependency may be missing, attempting with aria2c";
		aria2c -x $CPUS "$url" -o $output;
	else
	echo "Axel and Aria2c command failed, dependency may be missing, reverting to slowest way possible";
	wget "$url" -O $output;
	fi
}

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
CONFIGFOLDER="$FPPVERSION-$CONFIGNAME.tar.gz"
GITCLONELINK="https://github.com/Birdthulu/Ishiiruka"
SdCardFileName="ProjectPlusSdCard215.tar.gz"
SdCardDlHash="0anckw4hrxlqn5i"
SdCardLink="http://www.mediafire.com/file/$SdCardDlHash/$SdCardFileName/file"

rm build -rf;
mkdir build; cd build;

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
NUMBEROFRECORDS = tar -tzf "$CONFIGFOLDER" | wc -l
tar -C "AppDir/usr/bin/" -xzf "$CONFIGFOLDER" --checkpoint-action='exec=printf "%d/'$NUMBEROFRECORDS' records extracted.\r" $TAR_CHECKPOINT' --totals

rm AppDir/usr/bin/Sys -r
mv AppDir/usr/bin/Binaries/* AppDir/usr/bin/ -f
rm AppDir/usr/bin/Binaries -r
rm "$CONFIGFOLDER"

LINUXDEPLOY_PATH="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
LINUXDEPLOY_FILE="linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_URL="${LINUXDEPLOY_PATH}/${LINUXDEPLOY_FILE}"

UPDATEPLUG_PATH="https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous"
UPDATEPLUG_FILE="linuxdeploy-plugin-appimage-x86_64.AppImage"
UPDATEPLUG_URL="${UPDATEPLUG_PATH}/${UPDATEPLUG_FILE}"

UPDATETOOL_PATH="https://github.com/AppImage/AppImageUpdate/releases/download/continuous"
UPDATETOOL_FILE="appimageupdatetool-x86_64.AppImage"
UPDATETOOL_URL="${UPDATETOOL_PATH}/${UPDATETOOL_FILE}"

DESKTOP_APP_URL="https://github.com/project-slippi/slippi-desktop-app"
DESKTOP_APP_SYS_PATH="./slippi-desktop-app/app/dolphin-dev/overwrite/Sys"

APPDIR_BIN="./AppDir/usr/bin"

# Grab various appimage binaries from GitHub if we don't have them
if [ ! -e ./linuxdeploy ]; then
	download_file ${LINUXDEPLOY_URL} ./linuxdeploy
	chmod +x ./linuxdeploy
fi
if [ ! -e ./linuxdeploy-update-plugin ]; then
	download_file ${UPDATEPLUG_URL} ./linuxdeploy-update-plugin
	chmod +x ./linuxdeploy-update-plugin
fi
if [ ! -e ./appimageupdatetool ]; then

	download_file ${UPDATETOOL_URL} ./appimageupdatetool
	chmod +x ./appimageupdatetool
fi

OUTPUT="FasterProjectPlus-$FPPVERSION-x86_64.AppImage" ./linuxdeploy --appdir AppDir/ --output appimage