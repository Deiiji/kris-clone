#!/bin/bash

SCRIPTSRC=`readlink -f "$0" || echo "$0"`
RUN_PATH=`dirname "${SCRIPTSRC}" || echo .`

install_prefix=${RUN_PATH}/..

function install_desktop_entry()
{
    local installation_prefix="$1"
    local desktop_entries_dir="$2"

    local desktop_entry="\
[Desktop Entry]\n\
Name=Kirstens Viewer S21\n\
Comment=Client for the On-line Virtual World, Second Life\n\
Exec=${installation_prefix}/Runme-kirstens-S21\n\
GenericName=Kirstens Viewer S21\n\
Icon=${installation_prefix}/kv_s21.png\n\
Terminal=false\n\
Type=Application\n\
Categories=Application;Network;\n\
StartupNotify=true\n\
X-Desktop-File-Install-Version=3.0"

    echo " - Installing menu entries in ${desktop_entries_dir}"
    mkdir -vp "${desktop_entries_dir}"
    echo -e $desktop_entry > "${desktop_entries_dir}/kirsten-viewer.desktop" || "Failed to install application menu!"
}

if [ "$UID" == "0" ]; then
    # system-wide
    install_desktop_entry "$install_prefix" /usr/local/share/applications
else
    # user-specific
    install_desktop_entry "$install_prefix" "$HOME/.local/share/applications"
    xdg-icon-resource install ${RUN_PATH}/../kv_s21.png --size 128 --novendor
    xdg-desktop-icon install $HOME/.local/share/applications/kirsten-viewer.desktop
fi
