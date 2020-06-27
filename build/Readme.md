# Build ORC FDW
This file contains the necessary instructions to build ORC FDW package along with its dependencies.

## [Build Apache ORC Library](orc/Readme.md)
The default build behavior for Apache ORC generates a static library. Follow the instructions provided **[here](orc/Readme.md)** to
build shared libraries.

## Build ORC FDW
### Pre-Requisites
- PostgreSQL Server version 12 or above
- pg_config in path (preferred).
- Necessary build tools on the platform including a g++ compiler that supports c++11.
- Ensure that you have followed build instructions for Apache ORC package as mentioned earlier in this document.

### Build Steps
From within the ORC FDW source directory, you may issue one of the following commands:
- make
- make install

The build has been tested to work smoothly on **CentOS 7** and **Ubuntu 18.04**. There are some additional steps required to build on
**CentOS 6 x86_64**.

#### CentOS 6 x86_64
On CentOS 6 x86_64, the default g++ compiler version does not support c++11. You can however, get around this problem by installing 
**uncertified** but functional packages on it. Following are the details.

As a user with root priviledges, run the following statements:
- wget https://people.centos.org/tru/devtools-2/devtools-2.repo -O /etc/yum.repos.d/devtools-2.repo
- yum install devtoolset-2-gcc devtoolset-2-binutils devtoolset-2-gcc-c++

As a build user, run the following statements:
- export CXX=/opt/rh/devtoolset-2/root/usr/bin/g++
- export CMAKE_CXX_COMPILER=$CXX
- sed -i "s:^CXX\>:#CXX:g" "$(pg_config --libdir)/pgxs/src/Makefile.global"

Now you may proceed on with the build steps mentioned earlier in this document.

### Running Testsuite
You need to "make install" ORC FDW before running testsuite. 
- You have installed (run make install) ORC FDW for the desired PostgreSQL server.
- Ensure that your desired PostgreSQL server is running
- You also need to export a shell variable **ORC_FDW_DIR** in the shell to be used for running test suite. ORC_FDW_DIR should point to ORC FDW source folder.
