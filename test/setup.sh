#!/bin/bash

set -e # stop on errors
set -x # show commands as they run

DATAROOT=test/data
SAFE_UID=100
SAFE_GID=100

######################################################

setup_res_file() {
	cp -a $DATAROOT/res_file/ORIG/* $DATAROOT/res_file/

	chown $SAFE_UID:$SAFE_GID $DATAROOT/res_file/fstab
	chmod 0644 $DATAROOT/res_file/fstab
	chmod 0660 $DATAROOT/res_file/sudoers
}

setup_res_group() {
	:
}

setup_res_user() {
	:
}

setup_userdb() {
	rm -f test/tmp/passwd.new
	rm -f test/tmp/shadow.new
	rm -f test/tmp/group.new
	rm -f test/tmp/gshadow.new
}

######################################################

setup_res_file
setup_res_group
setup_res_user
setup_userdb
