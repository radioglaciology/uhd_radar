## Running Unit Tests
Note: In order to run the Python unit tests, you must have **Pytest** installed, which is not a part of `environment.yaml`. This can be installed with the command `conda run -n uhd conda install pytest`.

##### A Makefile is provided for automating the process of building and running Python and C++ unit tests. From the top level `uhd_radar/` directory, run the command `make test`. This will print the results from the C++ testing program to the terminal, followed by the results from the Python testing program. If any tests fail, the whole program quits.

If you would like to run only the Python tests, then from the top level `uhd_radar/` directory run the command `pytest tests/`. You can a specific Python test with the command `pytest path/to/test_file.py::test_function_name`. Pytest offers many other features such as running all tests whose name contains a specific keyword using `pytest -k "keyword"`.

To run only the C++ tests, you must first compile the tests as follows:
* `cd sdr/build/`
* `cmake ..`
* `cmake --build .`

Then either run all the C++ tests with `ctest --output-on-failure`, or run a specific test with `ctest -R TestSuiteName.TestCaseName`. To find all available test names, run `ctest -N`.
****
## Writing Unit Tests
All unit tests should be made within the `tests/` directory, with the same file structure as the source code, and all unit test files should be named `test_<source_file>.<type>`. For example, a unit test for the `generate_out_filename()` function within `sdr/utils.cpp` should be in the `tests/sdr/test_utils.cpp` file. All unit tests for all functions within some source file should be in the same testing file.

### Python
Python tests are required to have a specific naming scheme in order to be detected by Pytest. **They must be within the `tests/` directory, in a file whose name starts with `test_` and each test function name must also begin with `test_`. For example, `tests/preprocessing/test_generate_chirp.py::test_chirp_length()`**

The tests must contain an `assert` statement that compares the actual result to the expected result. This is what determines whether the test passes or fails. You may have multiple `assert` statements in a single test, however the test will end after the first failure. To allow for multiple failures in a single test, see [pytest-check](https://pypi.org/project/pytest-check/). An optional custom error message can be printed with `assert <condition>, f"Custom error message with {some_variable}"`.

As defined in `pytest.ini`, all Python tests are run from the root (top-level) directory. Therefore, any imported files must use the path relative to this directory, for example `from preprocessing.generate_chirp import generate_chirp`.

Python unit test files are not required to have a main function, though one may be useful during the debugging process.
###### Example Python test:

    from source_dir.file import my_function

    def test_my_function():
        ... code ...
        assert actual_val == expected_val, "Optional custom message upon failure"

### C++
C++ tests are compiled using **CMake** and run using **Google Test**.

Tests should follow the same file structure outlined above, though the name of each test works slightly differently. Each Google Test requires a name for test suite as well as for the individual test case. The test suite name should be the function being tested (ex.`GenerateOutFilename`), while the test case name should describe the specific case being tested (ex. `OneOutput`). All test cases for the same function should share the same test suite name.

The tests must contain at least one `EXPECT_*` statement that compares the actual result to the expected result. It is important to note that `EXPECT_*` statements are preferred over `ASSERT_*` statements as they generate non-fatal errors and allow for multiple failures to be reported. `ASSERT_*` statements abort the current process, potentially skipping any cleanup steps. A list of all `EXPECT_*` statement types can be found on the [GoogleTest Docs](https://google.github.io/googletest/reference/assertions.html), but common ones are `EXPECT_EQ, EXPECT_GT, EXPECT_TRUE, EXPECT_STREQ, EXPECT_NEAR`. An optional custom error message may be streamed into an `EXPECT_*` statement with the `<<` operator, as shown below.

All test files must include the GTest header with `#include <gtest/gtest.h>` as well as importing the source file being tested with `#include "../../<source_dir>/<file>.hpp"`. It is important to note that while the import path is relative to the test file's location, you must use a relative file path when opening a file at runtime. This can be done by adding a path macro the CMake file that defines the path from the CMake source directory to your file. See the CMake file and example test below for more details.

**After creating a test file, you must add it to CMake for it to be compiled and run by Google Test!** To do this, you must add the following sections of code to `tests/CMakeLists.txt` (if the file structure is set up correctly, `source_dir` should match `test_dir`). For better readability, paste each section from below into the matching section the CMake file (ex. add_executable should be with all the other existing calls to add_executable).
    
    # Add any required packages here. Ex:
    # find_package(Boost REQUIRED COMPONENTS filesystem)
    ...
    add_executable(test_<file>
        <test_dir>/test_<file>.cpp
        ../<source_dir>/<file>.cpp
    )
    ...
    # If you are opening files in your test, add the path from this CMake file to your files directory here. Ex:
    # target_compile_definitions(test_<file> PRIVATE CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../config")
    target_include_directories(test_<file> PRIVATE ../<source_dir>)
    target_link_libraries(test_<file>
        gtest_main
        # Other required packages go here such as UHD, yaml-cpp, or Boost::filesystem
        # Make sure to call find_package() above
    )
    ...
    gtest_discover_tests(test_<file>)

##### Unit Tests for Hardware Components
If there are variables that cannot only be tested with software, hardware tests are needed. These are unit tests that only execute when the code is run in hardware. An example of this is setting up the SDR. There are variables in the code that cannot be tested without a hardware connection but still need to be tested. 

To do this, first make sure the functions that are being tested are either public or preferrably in a friend class at the top of the testing file. Then, write normal unit tests as described above. Once the test is written, go into `Makefile` and in the `software-cxx-testing` line, add `-E "<suiteName>"` for all the hardware testing suites that have been created. If `-E` is already there with other testing suites, simply add to the line with your testing suite so it will look like this, 

    -E "<existingSuiteName>|<yourSuiteName>"

Do the same thing as above in the `.github\workflows/unit-tests.yml` file under `name: Run C++ Tests` in the `run:` section after all of the other flags are listed.

This will exclude the hardware tests from running when only the software is run. When running only software tests, run `make software-test`. Then, when hardware is connected, run `make hardware-test` which will run both the software and hardware unit tests.

###### Example C++ test:

    #include <gtest/gtest.h>
    #include "../../source_dir/file.hpp"

    TEST(TestSuiteName, TestCaseName) {
        ... code ...
        const string kConfigFile = string(CONFIG_DIR) + "/default.yaml"; // Example of how to use relative file path
        ...
        EXPECT_EQ(actual_val, expected_val) << "Optional custom message upon failure";
        EXPECT_TRUE(boolean_val);
    }
