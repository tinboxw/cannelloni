#!/bin/bash

function color_text(){ echo -e "\e[1;$2m$1\e[0m"; }
function log_r(){ text=$(color_text "$*" "31"); echo "$text";}
function log_g(){ text=$(color_text "$*" "32"); echo "$text";}
function log_y(){ text=$(color_text "$*" "33"); echo "$text";}
function log_d(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=dbg; echo  "[$time - $mode] $*"; }
function log_w(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=wan; log_y "[$time - $mode] $*"; }
function log_e(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=err; log_r "[$time - $mode] $*"; }
function log_i(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=inf; log_g "[$time - $mode] $*"; }
function log_t(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=tra; log_g "[$time - $mode] $*"; }
function log_f(){ time=`date +"%Y-%m-%d %H:%M:%S.%NS"`; mode=ftl; log_r "[$time - $mode] $*"; exit 1; }

function say_usage(){
  log_y ""
  log_y "Usage: $0 [options] [target]"
  log_y "  If no target is specified, all targets will be built by default."
  log_y ""
  log_y "Options:"
  log_y "  -h|--help         Show this help message"
  log_y "  -r|--release      Build the release version"
  log_y "  -d|--debug        Build the debug version"
  log_y "  -i|--install      Specified the install directory"
  log_y "  -a|--arch         Support [amd64|arm64], default use current system arch"
  log_y "  -v|--version      Specified Project version, default 0.0.0"
  log_y ""
}

function show(){
  echo ""
  log_y "Show build args:"
  echo -n "    Product version:       ";log_y "[$PRODUCT_VERSION]"
  echo -n "    Project version:       ";log_y "[$PROJECT_VERSION]"
  echo -n "    Build type:            ";log_y "[$BUILD_TYPE]"
  echo -n "    Arch:                  ";log_y "[$ARCH]"
  echo -n "    Install Directory:     ";log_y "[$INSTALL_DIR]"
  echo -n "    CMake args:            ";log_y "[$CMAKE_ARGS]"
  echo ""
}


set -eu;

#CURR_DIR=$(pwd)
SCRIPT_POS=$(readlink -f $0)
SCRIPT_DIR=$(dirname "$SCRIPT_POS")
CODE_DIR=$SCRIPT_DIR/..
BUILD_DIR=
TOOLCHAIN_FILE=
PKGS_DIR=$CODE_DIR/packages

ARCH=
INSTALL_DIR="opt/obss/candtu"
PRODUCT_VERSION=0.0.0
PROJECT_VERSION=0.0.0
BUILD_TYPE="Release"
CMAKE_ARGS=

# getopts
until [ $# -eq 0 ]; do
  case "$1" in
    --*=*) value=`echo $1|awk -F= '{print $2}'`;;
  esac

  case "$1" in
    #--version) show_version; exit 0;;
    -h|--help) usage; exit 0;;

    --product-version) PRODUCT_VERSION=$2; shift 2;;
    --product-version=*) PRODUCT_VERSION=$value; shift 1;;
    --project-version) PROJECT_VERSION=$2; shift 2;;
    --project-version=*) PROJECT_VERSION=$value; shift 1;;

    -i|--install) INSTALL_DIR=$2; shift 2;;
    --install=*) INSTALL_DIR=$value; shift 1;;

    -a|--arch) ARCH=$2; shift 2;;
    --arch=*) ARCH=$value; shift 1;;

    --release) BUILD_TYPE="Release"; shift 2;;
    --release=*) BUILD_TYPE="Release"; shift 1;;
    --debug) BUILD_TYPE="Debug"; shift 2;;
    --debug=*) BUILD_TYPE="Debug"; shift 1;;

    *) echo " unknown params $1" && say_usage && exit 1;;
  esac
done

[ ! -d ${PKGS_DIR} ] && mkdir -p $PKGS_DIR

cd $CODE_DIR
if [ x"$ARCH" == x"" ];then
  ARCH=`uname -m`
  BUILD_DIR=${CODE_DIR}/.obj-${ARCH}-linux-gnu
  TOOLCHAIN_FILE=
else
  BUILD_DIR=${CODE_DIR}/.obj-${ARCH}-linux-gnu
  TOOLCHAIN_FILE=${CODE_DIR}/cmake/toolchain-${ARCH}.cmake
  [ ! -f ${TOOLCHAIN_FILE} ] && log_r "Not found toolchain file: $TOOLCHAIN_FILE" && exit 1
fi

CMAKE_ARGS+=" -B ${BUILD_DIR} "
CMAKE_ARGS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE} "
CMAKE_ARGS+=" -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} "
CMAKE_ARGS+=" -DCMAKE_BUILD_TYPE=${BUILD_TYPE} "
CMAKE_ARGS+=" -DPRODUCT_VERSION=${PRODUCT_VERSION} "
CMAKE_ARGS+=" -DPROJECT_VERSION=${PROJECT_VERSION} "


show

cmake ${CMAKE_ARGS} ${CODE_DIR}
make  -C ${BUILD_DIR} -j
cd ${BUILD_DIR} && cpack
cd -

mv ${BUILD_DIR}/*deb ${PKGS_DIR}/

echo ""
log_g "Build successfully, package:"
for f in `ls ${PKGS_DIR}/`;do
  log_y "- $f"
done
echo ""