# !/bin/bash

# This export is for the terminal's font.
export TERM=xterm-color

# Check for existence of buildifier.
which buildifier >/dev/null
if [[ $? != 0 ]]; then
  printf "Failed to find 'buildifier'\n"
  exit 1
fi

# Find all files we want to check 
IFS=:
bzl_files_paths=$(find . -name '*.bzl' -o -name '*.bazel' -o -name 'BUILD' -o -name 'WORKSPACE')
unset IFS

if [[ ${#bzl_files_paths} == 0 ]]; then
    printf "There are no files to check!\n"
    exit 0
fi

status_exit=0

for file in ${bzl_files_paths}
do
    # Check if the specific file has correct code format.
    buildifier --lint=warn --warnings=-module-docstring,-function-docstring ${file}
    format_status=$(echo $?)

    if [[ ${format_status} != 0 ]]; then
        tput bold # Bold text in terminal.
        tput setaf 1 # Red font in terminal.
        printf "${file} file has incorrect code format!\n"
        tput sgr0
        tput bold # Bold text in terminal.
        printf "Command to format ${file}: "
        tput setaf 2 # Green font in terminal.
        printf "buildifier "${file}"\n"
        tput sgr0 # Reset terminal.
        tput bold
        printf "If you have no buildifier install it with the command: \n"
        tput setaf 2 # Green font in terminal.
        printf "brew install buildifier\n"
        tput sgr0 # Reset terminal.
        status_exit=1
    fi
done

exit ${status_exit}
