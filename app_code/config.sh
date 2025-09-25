#! /bin/sh

PROJECT_DIR=$PWD   
TOOL_CHAIN_PATH=$PROJECT_DIR/../../Ingenic-SDK-T23-1.1.2-20240204-en/resource/toolchain/gcc_540/mips-gcc540-glibc222-64bit-r3.3.0.smaller

LIB_AAC_ROOT_DIR="lib/thirdparty-lib/aac"
AAC_OUT_DIR=$LIB_AAC_ROOT_DIR/build

if [ ! -d "$LIB_AAC_ROOT_DIR/aac-src" ]; then
	mkdir -p $LIB_AAC_ROOT_DIR/build 
	cd $LIB_AAC_ROOT_DIR
	git clone https://github.com/mstorsjo/fdk-aac aac-src
	cd aac-src
	autoreconf -fiv
else
	cd $LIB_AAC_ROOT_DIR/aac-src
fi


export CROSS_PREFIX=$TOOL_CHAIN_PATH/bin/mips-linux-gnu  # or aarch64-linux-gnu, etc.
export SYSROOT=$TOOL_CHAIN_PATH/bin/../mips-linux-gnu/libc
export HOST=mips-linux-gnu
export CC=${CROSS_PREFIX}-gcc
export CXX=${CROSS_PREFIX}-g++
export AR=${CROSS_PREFIX}-ar
export LD=${CROSS_PREFIX}-ld
export RANLIB=${CROSS_PREFIX}-ranlib
export STRIP=${CROSS_PREFIX}-strip

./configure \
  --host=$HOST \
  --prefix=$PROJECT_DIR/$AAC_OUT_DIR \
  --enable-static \
  --disable-shared
    
make -j$(nproc)
make install 

libtool --finish $PROJECT_DIR/$AAC_OUT_DIR/lib

cd ../

if [ ! -d "build/lib" ]; then
  echo "Fail to build aac static lib ! exit .....\n"
  exit 0 
else 
  echo "Done ! ... lib-aac copied to project lib dir .....\n"
fi

cd ../


#exit 0 



echo "#lets build ffmpeg static lib   .................................................................................\n"

LIB_FFMPEG_ROOT_DIR="ffmpeg"
FFMPEG_OUT_DIR=keo-ffmpeg-sttic-lib
LIB_AAC_PATH=$PROJECT_DIR/$AAC_OUT_DIR

if [ ! -d "$LIB_FFMPEG_ROOT_DIR/ffmpeg_src/$FFMPEG_OUT_DIR" ]; then 
	if [ -d "$LIB_FFMPEG_ROOT_DIR" ]; then
		rm -rf $LIB_FFMPEG_ROOT_DIR
	fi
	
	mkdir -p  $LIB_FFMPEG_ROOT_DIR
	cd $LIB_FFMPEG_ROOT_DIR 
	git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg_src
	cd ffmpeg_src

	make distclean
	git pull && git checkout n6.1
else
	cd $LIB_FFMPEG_ROOT_DIR/ffmpeg_src
  	make clean 
fi

cc=$TOOL_CHAIN_PATH/bin/mips-linux-gnu-gcc
export PATH=$PATH:$TOOL_CHAIN_PATH/bin/

#mips-linux-gnu-gcc --version

export PKG_CONFIG_PATH=$LIB_AAC_PATH/lib/pkgconfig

./configure \
  --prefix=$PWD/$FFMPEG_OUT_DIR \
  --arch=mips \
  --target-os=linux \
  --cross-prefix=mips-linux-gnu- \
  --cpu=mips32r2 \
  --enable-cross-compile \
  --disable-asm \
  --disable-doc \
  --disable-debug \
  --disable-network \
  --disable-everything \
  --enable-small \
  --enable-ffmpeg \
  --enable-decoder=mp3 \
  --enable-decoder=pcm_s16le \
  --enable-decoder=h264 \
  --enable-encoder=pcm_s16le \
  --enable-parser=mpegaudio \
  --enable-parser=h264 \
  --enable-demuxer=mp3 \
  --enable-demuxer=h264 \
  --enable-muxer=mpegts \
  --enable-muxer=wav \
  --enable-muxer=pcm_s16le \
  --enable-muxer=mp4 \
  --enable-bsf=h264_mp4toannexb \
  --enable-protocol=file \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-libvpx \
  --enable-nonfree \
  --enable-libopus \
  --enable-muxer=matroska \
  --enable-gpl \
  --enable-nonfree \
  --enable-libfdk-aac \
  --extra-cflags="-I$LIB_AAC_PATH/include/" \
  --extra-ldflags="-L$LIB_AAC_PATH/lib" \
  --enable-static \
  --disable-shared 

# make clean && make -j$(nproj) 
make install -j$(nproc)

if [ ! -d "keo-ffmpeg-sttic-lib" ]; then
	echo "fail to create ffmpeg static lib !"
	exit 0 
fi

cd ../../../../

if [ ! -d "build" ]; then 
	mkdir -p build/debug 
	mkdir -p build/release 
fi














#export TOOL_CHAIN_DIR=/home/afzal/ingenic/Ingenic-T23-SDK/T23-PIKE/T23_PIKE_20240325/SDK_20240204/ISVP-T23-1.1.2-20240204/Ingenic-SDK-T23-1.1.2-20240204-en/resource/toolchain/gcc_540/mips-gcc540-glibc222-64bit-r3.3.0.smaller

PROJ_DIR=$PWD
OPEN_SSL_SRC_DIR=lib/thirdparty-lib/openssl
#OPEN_SSL_INSTALL_DIR=$PROJ_DIR/$OPEN_SSL_SRC_DIR/open_ssl_static_lib

if [ ! -d "$OPEN_SSL_SRC_DIR" ]; then 
	mkdir -p $OPEN_SSL_SRC_DIR 
fi
cd $OPEN_SSL_SRC_DIR

#if [ -d "open_ssl_static_lib" ]; then
#	rm -rf open_ssl_static_lib 
#fi
#mkdir open_ssl_static_lib



#echo "using --cross-compile-prefix   and path variable"

#export CROSS_COMPILE=mips-linux-gnu-
#export PATH=$TOOL_CHAIN_DIR/bin:$PATH

#if [ -f "openssl-1.1.1w.tar.gz" ]; then
# 	rm openssl-1.1.1w.tar.gz 
#fi

#wget https://www.openssl.org/source/openssl-1.1.1w.tar.gz
#tar xvf openssl-1.1.1w.tar.gz
#cd openssl-1.1.1w  

#./Configure linux-generic32 \
#    --prefix=$OPEN_SSL_INSTALL_DIR \
#    --openssldir=$PWD/install-mips no-shared no-async \
#    --cross-compile-prefix=$CROSS_COMPILE

#make -j$(nproc)
#make install_sw

echo "using CROSS_PREFIX "


export CROSS_PREFIX=$TOOL_CHAIN_DIR/bin/mips-linux-gnu-
if [ -f "openssl-1.1.1w.tar.gz" ]; then
 	rm openssl-1.1.1w.tar.gz 
fi


wget https://www.openssl.org/source/openssl-1.1.1w.tar.gz
tar xvf openssl-1.1.1w.tar.gz
cd openssl-1.1.1w  

./Configure linux-generic32 \
   --prefix=$PWD/install-mips \
   --openssldir=$PWD/install-mips no-shared no-async


make -j$(nproc)
make install_sw

echo "open ssl build ok .... .... ................................"
 




export OPEN_SSL_DIR=/home/mahmudul/mahmudul_workspace/keo_cam_ingenic/ipcam_code/ip-cam-firmware-t23/apps/keo-cam/lib/thirdparty-lib/openssl/openssl-1.1.1w/install-mips
export PATH=$PATH:$OPEN_SSL_DIR    


SRT_INSTALL_DIR=/home/mahmudul/mahmudul_workspace/keo_cam_ingenic/ipcam_code/ip-cam-firmware-t23/apps/keo-cam/lib/thirdparty-lib/srt/build/

cd $PROJ_DIR/lib/thirdparty-lib 

if [ ! -d "srt" ]; then
  git clone https://github.com/Haivision/srt.git
fi

cd srt
if [ -d "build" ]; then
  rm -rf build
fi
mkdir build 
cd build
 
export CXXFLAGS="-DSRT_SYNC_ATOMIC" 

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/home/mahmudul/mahmudul_workspace/keo_cam_ingenic/ipcam_code/ip-cam-firmware-t23/apps/keo-cam/mips-toolchain.cmake \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_STATIC=ON \
  -DENABLE_SHARED=OFF \
  -DENABLE_APPS=OFF \
  -DENABLE_CXX11=ON \
  -DCMAKE_CXX_FLAGS="-DSRT_SYNC_ATOMIC" \
  -DENABLE_LOGGING=OFF \
  -DCMAKE_INSTALL_PREFIX=$SRT_INSTALL_DIR \
  -DSRT_USE_OPENSSL_STATIC_LIBS=ON \
  -DCMAKE_PREFIX_PATH=$OPEN_SSL_DIR \
  -DOPENSSL_ROOT_DIR=$OPEN_SSL_DIR \
  -DOPENSSL_INCLUDE_DIR=$OPEN_SSL_DIR/include \
  -DOPENSSL_CRYPTO_LIBRARY=$OPEN_SSL_DIR/lib/libcrypto.a \
  -DOPENSSL_SSL_LIBRARY=$OPEN_SSL_DIR/lib/libssl.a \
  -DSRT_USE_OPENSSL_STATIC_LIBS=ON \
  -DUSE_ENCLIB=openssl-evp \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS="-latomic" \
  -DCMAKE_SHARED_LINKER_FLAGS="-latomic"

  
make -j$(nproc)
make install


