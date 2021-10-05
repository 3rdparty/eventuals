#!/bin/bash

# This export is for the terminal's font.
export TERM=xterm-color

# We need check_line_length() from this script cause we want to check if any line has
# more than 80 characters.
source ./tools/check_line_length.sh

# Check for existence of clang-format.
which clang-format >/dev/null
if [[ $? != 0 ]]; then
  printf "Failed to find 'clang-format'\n"
  exit 1
fi

# Find all files we want to check in the root directory.
IFS=:
file_paths=$(find . -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.proto')
unset IFS

# Check every file for correct code style.
check(){
  if [[ ${#file_paths} == 0 ]]; then
    printf "There are no files to check!\n"
    exit 0
  fi  

  status_exit=0

  for file in ${file_paths}
  do
    # Check if the specific file has correct code format.
    #
    # If the file is well formatted the command below will return 0. 
    # We use this fact to print a more helpful message.
    clang-format-12 --dry-run -Werror --ferror-limit=0  "${file}"
    format_status=$(echo $?)

    if [[ ${format_status} != 0 ]]; then
      tput bold # Bold text in terminal.
      tput setaf 1 # Red font in terminal.
      printf "${file} file has incorrect code format!\n"
      tput sgr0
      tput bold # Bold text in terminal.
      printf "Command to format ${file}: "
      tput setaf 2 # Green font in terminal.
      printf "clang-format --style=Google -i "${file}"\n"
      tput sgr0 # Reset terminal.
      exit 1
    fi

    # Check if any line has more than 80 characters.
    check_line_length "${file}"
    status_check_line_length=$(echo $?)
    if [[ ${status_check_line_length} != 0 ]]; then
      status_exit=${status_check_line_length}
    fi
  done
  exit ${status_exit}
} # End of check() function.

check
