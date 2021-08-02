# Installation

## Windows installation

Here we offer a proven way to configure the development environment for [Bazel](https://bazel.build) in the [Windows](https://www.microsoft.com) operating system.

1. Install a convenient development environment [Visual Studio Code](https://code.visualstudio.com/download) that is adapted to work with [Bazel](https://bazel.build).
2. Run [Visual Studio Code](https://code.visualstudio.com/download) and install the necessary extensions:
    1. [Bazel plugin](https://marketplace.visualstudio.com/items?itemName=BazelBuild.vscode-bazel). This extension provides support for Bazel in Visual Studio Code.
    2. [C/C++ plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools). The C/C++ extension adds language support for C/C++ to Visual Studio Code, including features such as IntelliSense and debugging.
    3. [Clang-format plugin](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format). This extension allows you to comply with the clang format for your code. Read the plugin overview for configuration.
    Short instuction how you can use Visual Studio `clang-format`:
        1. Create a folder `.vscode`in your project folder.
        2. Create a file `settings.json` in the folder `.vscode`
        3. Add the data to the file (check the path to your `clang-format.exe`):
        ```
        {
        "clang-format.style": "Google",
        "clang-format.executable": "C:/Program Files (x86)/Microsoft Visual Studio/2019/
        Community/VC/Tools/Llvm/x64/bin/clang-format.exe",
        "editor.formatOnSave": true
        }
        ```
    4. [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb). This extension allows you to debug your code. Read the plugin overview for configuration.
3. Install [Bazel](https://bazel.build). Detailed installation instructions for Windows can be found here: [Installing Bazel on Windows](https://docs.bazel.build/versions/4.1.0/install-windows.html). This is an important step. You must follow all the instructions, otherwise you will get various errors at the compilation stage.
4. Install the latest version of the compiler [LLLM](https://llvm.org) ([LLVM Download Page](https://releases.llvm.org/download.html)).
5. Install [Git](https://git-scm.com/downloads)
6. Restart your PC.
7. Make an empty directory for your projects and clone the [stout-eventuals](https://github.com/3rdparty/stout-eventuals) via the git clone link:
```
https://github.com/3rdparty/stout-eventuals.git
```
8. Start [VS Code](https://code.visualstudio.com).
9. Open the stout-eventuals folder via VS Code.
10. Check the checkboxes about "Trust the authors".
11. VS Code -> Terminal -> New Terminal