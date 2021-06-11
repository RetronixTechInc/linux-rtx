Build Kernel use the build.sh

Step 1: 
Download toolchain tool and extract to the fold.
Please use the gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz

Define CROSS_COMPILE_PATH to extract fold at build.sh.
https://drive.google.com/drive/folders/183Me7tEWx8NbZA9SSSy5UBg80MqBo1gb?usp=sharing

Step 2:Enter kernel/kernel-4.9 fold.

    cd kernel/kernel-4.9

Step 3:Modify toolchain path.

    Define the CROSS_COMPILE_TOOL at build.sh

Step 4:build kernel,dtb and modules

    ./build.sh command.

Step 5:
Find uImage, modules, fireware and *.dtb at out/build/... fold if build success.
