# Installation

## MacOS installation

Here we offer a proven way to configure the development environment for [Bazel](https://bazel.build) in the [MacOS](https://www.apple.com) operating system.

1. Install a convenient development environment [Visual Studio Code](https://code.visualstudio.com/download) that is adapted to work with [Bazel](https://bazel.build).
2. Run [Visual Studio Code](https://code.visualstudio.com/download) and install the necessary extensions:
    1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
    2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
    3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.
    4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build).
Here we provide some details to help you:
    1. Check the presence of [Bazel](https://bazel.build) in your [MacOS](https://www.apple.com) using the following command in your terminal:
    ```
    $ bazel --version
    ```
    2. If you have no Bazel - install it using Homebrew. Install Homebrew(if needed):
    ```
    $ /bin/bash -c "$(curl -fsSL \ https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
    ```
    3. Install the Bazel package via Homebrew as follows:
    ```
    $ brew install bazel
    ```
    4. Upgrade to a newer version of Bazel using the following command(if needed):
     ```
    $ brew upgrade bazel
    ```
4. Make an empty directory for your projects and clone the [stout-eventuals](https://github.com/3rdparty/stout-eventuals) via the git clone link:
```
https://github.com/3rdparty/stout-eventuals.git
```
5. Start [VS Code](https://code.visualstudio.com).
6. Open the stout-eventuals folder via VS Code.
7. Check the checkboxes about "Trust the authors".
8. VS Code -> Terminal -> New Terminal