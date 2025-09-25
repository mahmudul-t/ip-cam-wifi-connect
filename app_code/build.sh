#! /bin/bash

NFS_DIR=/home/mahmudul/mahmudul_workspace/keo_cam_ingenic/nfs-drive
BUILD_DIR=./build/release
EXECUTABLE_NAME=keo-cam

EXE_FILE_PATH=$BUILD_DIR/$EXECUTABLE_NAME

cd ./src

#if [ "$1" == "clean" ]; then 
#	make clean 
#fi	
if [ "$1" == "clean" ]; then 
	make clean 
fi 	
make -j$(nproc)

cd ..

if [ -f "$EXE_FILE_PATH" ]; then
   cp $BUILD_DIR/$EXECUTABLE_NAME $NFS_DIR
   echo "$EXE_FILE_PATH copied to $NFS_DIR"
else
   echo "Fail to create $EXECUTABLE_NAME  !"
fi

