#!/bin/bash

# -- USAGE --
# Option 1: Run script without any arguments for standard ORC build. It will build a set of shared librares.
# Option 2: To run individual steps, call the script the required function name.
# Option 3: Run script with "copy_orc_install <DEST PATH>" commandline arguments to copy libs and includes to a destination folder


# Source Root
export SOURCE_ROOT_DIR=$PWD
export BUILD_DIR=$SOURCE_ROOT_DIR/build

# Let's export the required CMAKE_CXX_FLAGS
export CMAKE_CXX_FLAGS="-fPIC"

# Add libproto shared objects to LD_LIBRARY_PATH
# - Required as rpath is set later and build will otherwise fail
export LD_LIBRARY_PATH=$BUILD_DIR/c++/libs/thirdparty/protobuf_ep-install/lib

# Modifies an individual cmake build file; changes static to shared
replace_static_shared()
{
	filename="$1"

	sed -i "s:\(add_library.*\)STATIC:\1SHARED:g" $filename
	sed -i "s:STATIC_LIB:SHARED_LIB:g" $filename
	sed -i "s:\(set..ZSTD_SHARED_LIB_NAME\).*zstd:\1 zstd:g" $filename
	sed -i "s:DBUILD_SHARED_LIBS=OFF:DBUILD_SHARED_LIBS=ON:g" $filename
	sed -i "s:DBUILD_SHARED_HDFSPP=FALSE:DBUILD_SHARED_HDFSPP=TRUE:g" $filename
	sed -i "s:hdfspp_static:hdfspp:g" $filename
}

# Replace all static references to shared in cmake_modules
configure_shared()
{
	sed -i "s:\(add_library.*\)STATIC:\1SHARED:g" $SOURCE_ROOT_DIR/c++/src/CMakeLists.txt

	for f in $SOURCE_ROOT_DIR/cmake_modules/*;
	do
		replace_static_shared "$f"
	done
}

# Configure the ORC package
configure()
{
        cmake_command="cmake"
        cmake_major_version="$(${cmake_command} 2>/dev/null --version | head -1 | awk '{print $3}' | cut -f1 -d".")"

        if [[ "$cmake_major_version" != "3" ]];
        then
                cmake_command="cmake3"
        fi

        ${cmake_command} -E env CXXFLAGS="-fPIC" CMAKE_CXX_FLAGS="-fPIC" ${cmake_command} .. -DBUILD_JAVA=OFF -DBUILD_SHARED_LIBS=ON -DBUILD_CPP_TESTS=OFF
}

# Builds the configure ORC package
build()
{
	make package
}

# Not used as part of build. Just a helper function to copy includes and libs to FDW folder
copy_orc_install()
{
	dest_libdir="$1/lib"
	dest_incdir="$1/include"

	if [[ -z "$1" ]];
	then
		echo "[Error] destination path not specified for copying include and lib files."
		exit 1
	fi

	mkdir -pv $dest_incdir
	mkdir -pv $dest_libdir

	ldd $BUILD_DIR/c++/src/liborc.so | awk '{print $3;}' | grep thirdparty | xargs -I{} echo "cp -rp \$(dirname {})/*.so* $dest_libdir" | sh

	cp -rp $BUILD_DIR/c++/src/liborc.so $dest_libdir
	chrpath -r '${ORIGIN}' $dest_libdir/liborc.so

	cp -rp $BUILD_DIR/_CPack_Packages/Linux/TGZ/ORC-*/include/orc $dest_incdir
}

# Let's create a separate folder for build files to avoid cluttering the source
mkdir -pv $BUILD_DIR
pushd $BUILD_DIR

# Call specific function passed as an argument to this function passing any preceeding arguments
# - otherwise, let's do standard build.
if [[ ! -z "$1" ]];
then
	($1 "${@:2}")
else
	configure_shared
	configure
	build
fi

popd

