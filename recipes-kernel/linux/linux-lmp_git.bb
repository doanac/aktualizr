LINUX_VERSION ?= "4.18.7"

FIO_LMP_GIT_URL ?= "source.foundries.io"
FIO_LMP_GIT_NAMESPACE ?= ""

SRCREV_machine = "fd0a4374fc94981ff996c607632b76a4722f584e"
SRCREV_meta = "ec875b44e9063b64e388ef6b71e195f79e9cff5d"
KBRANCH = "linux-v4.18.y"

LIC_FILES_CHKSUM = "file://COPYING;md5=bbea815ee2795b2f4230826c0c6b8814"

SRC_URI = "git://${FIO_LMP_GIT_URL}/${FIO_LMP_GIT_NAMESPACE}linux.git;protocol=https;branch=${KBRANCH};name=machine; \
    git://${FIO_LMP_GIT_URL}/${FIO_LMP_GIT_NAMESPACE}lmp-kernel-cache.git;protocol=https;type=kmeta;name=meta;branch=master;destsuffix=${KMETA} \
"

KMETA = "kernel-meta"

require linux-lmp.inc
require linux-lmp-machine-custom.inc