# Building/Testing
Currently we only support Bazel and expect/use C++17.

1. Setup the file .bazelrc for your OS:
    - Mac OS:
    ```
    bazel build --config=macos :eventuals
    ```
    - Windows:
    ```
	bazel build --config=windows :eventuals
	```
    - Linux:
	```
	bazel build --config=linux :eventuals
	```
2. Build the library Eventuals with:
```
bazel build :eventuals
```
After the running command above the new directory "bazel-bin" in your project folder is created which includes eventuals.lib. This library is needed for the next step.

3. Build the tests with:
```
bazel build test:eventuals
```
Build and run the tests with:
```
bazel test test:eventuals
```
