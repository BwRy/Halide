This directory contains code for writing very simple apps to test, prototype, demo, or debug Halide features. You may create apps for iOS, OS X, or PNACL using a generator, test function, and a pair of CMAKE function calls.

Very Simple Apps
================

The apps are very simple and consist of about 400 lines of boiler plate code automatically included in your project by the CMAKE functions. The apps are wired up to display `halide_print` messages and may use functions that are part of the HalideRuntime. New runtime functions like `halide_buffer_display` are available to the tests to display input or output images in the app. 

The basic app structure and behavior is the same across all platforms. You can use this mechism to prototype features on mobile devices and then distribute PNACL demos of the feature over the web. You can develop on your laptop using the OS X app and then demo on an iPad.

Test Functions
--------------

The test function is a simple C function of the form `bool test(void)` that contains logic to initialize input, call the Halide generated function, and then verify the output. The test function may use extra runtime functions to display `buffer_t` contents or load buffer data from a URL.

    buffer_t g = {
    ...
    };
    halide_print(NULL,"Testing the CPU target\n");
    example(runtime_factor,&g);
    halide_buffer_display(&g);

You can print error messages to the app using `halide_error` and the return  value from the test function will be printed by the app as well. Printed  messages may contain HTML.

Building Apps
-------------

The apps may be build using CMAKE and Xcode on OS X. You should start with a build of Halide based on the instructions in [README.md](../../README.md) in the Halide repository root directory. Build a static library version of libHalide.a. Follow the PNACL directions if you plan to build PNACL apps.

Each app is built using a separate CMAKE project per platform using ahead-of-time compilation.

Create a CMakeLists.txt file for your app and include [HalideAppTests.cmake](HalideAppTests.cmake).  

Select a Halide::Generator derived class to include in the app. Write a test function and put it in a separate .cpp file.

Use the `halide_add_*_app()` and `halide_add_*_generator_to_app()` function variants (for iOS, Mac OS X, PNACL, etc.) to add the platform specific boilerplate code and Halide generated code for your app. 

The CMakeLists.txt file must contain a few other  directives like a `cmake_minimum_required version()` and `project()`. See [ios/CMakeLists.txt](ios/CMakeLists.txt) for a complete example.

Create a build directory for the app and platform. From the base directory of your Halide build, along side the `build` directory you created following [README.md](../../README.md):

    mkdir build-ios
    cd build-ios

Run the ccmake program inside the build directory:

    ccmake -GXcode ../test/platforms/ios

Set the following CMake variables:

1. HALIDE_INCLUDE_PATH the path to the directory containing Halide.h and 
HalideRuntime*.h. Following the directions for building Halide with CMake, it 
is likely `~/Halide/build/include`
2. HALIDE_LIB_PATH the path to the file libHalide.a e.g. 
`~/Halide/build/lib/Debug/libHalide.a`
3. For PNACL apps only: `NACL_SDK_ROOT` the path to the directory containing the 
whole NACL sdk.
4. For PNACL apps only: `NACL_PEPPER_VERSION` the version number of the pepper_<i>nn</i> 
directory to use inside the NACL SDK. The default value reflects the Pepper API
version tested with this build.

Configure and generate the CMake project. Build the target that you defined in the CMakeLists.txt file.

You can run PNACL targets by launching the webserver included in the NACL SDK and serving the `bin/` directory from the CMAKE build. For example, if you are in the build directory and the NACL SDK is installed in your home directory, you would issue:

    xcodebuild -target test_pnacl && python ~/nacl_sdk/pepper_41/tools/httpd.py --no-dir-check -C bin

