# Installation

## Linux installation

Here we offer a proven way to configure the development environment for [Bazel](https://bazel.build) in the [Linux](https://www.linux.org) operating system.

1. Install a convenient development environment [Visual Studio Code](https://code.visualstudio.com/download) that is adapted to work with [Bazel](https://bazel.build).
2. Run [Visual Studio Code](https://code.visualstudio.com/download) and install the necessary extensions:
    1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
    2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
    3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.    
    4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build).
4. Install the latest version of the compiler [LLLM](https://llvm.org) ([LLVM Download Page](https://releases.llvm.org/download.html)).
5. Install [Git](https://git-scm.com/downloads)
6. Make an empty directory for your projects and clone the [stout-eventuals](https://github.com/3rdparty/stout-eventuals) via the git clone link:
```
https://github.com/3rdparty/stout-eventuals.git
```
7. Start [VS Code](https://code.visualstudio.com).
8. Open the stout-eventuals folder via VS Code.
9. Check the checkboxes about "Trust the authors".
10. VS Code -> Terminal -> New Terminal