# Building Apache ORC Library
This file will provide you steps to be build shared library for Apache ORC package. This has been verified to work with version
1.6.2.

You may download the sources from **[Apache ORC release page](https://github.com/apache/orc/releases)**.

## Build Steps
The build has been verified on CentOS6 x86_64, CentOS 7 and Ubuntu 18.04. You need to install development packages including:
- cmake version 3.x.x
- c++ compiler supporting c++11 standard
- chrpath utility

Once you have prepared the build environment, you may copy **[build-orc.sh script](build-orc.sh)** to the Apache ORC source folder. Then
simply run from within the Apache ORC source folder:
- ./build-orc.sh

Sit back and relax as this may take some time. Grab a cup of coffee or tea in the meantime.

Once the build is complete, you need to copy the generated libraries and header files to ORC FDW folder. You can do that by running
**build-orc.sh** script as:
- ./build-orc.sh copy_orc_install **<ORC FDW SOURCE PATH>**
where <ORC FDW SOURCE PATH> is the path where ORC FDW sources reside.

And with that, you are done.

In case you wish to explore Apache ORC build further, you may check their documentation for more details.
