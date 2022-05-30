# !/bin/bash

# File passed to this script as an argument in which we gonna
# update 'git_repository' bazel rule for eventuals 
# (WORKSPACE.bazel usually).
file=$1

# The latest commit for eventuals.
commit=$2

# The latest shallow_since for eventuals.
shallow_since=$3

if [[ ! -f "$file" ]]; then
  echo "$file doesn't exist"
  exit 1
fi

if [[ ${#commit} == 0 ]]; then
  echo "Missing or invalid commit value"
  exit 1
fi

if [[ ${#shallow_since} == 0 ]]; then
  echo "Missing or invalid shallow_since value"
  exit 1
fi

# This variable indicates whether or not we have found the correct
# 'git_repository' rule for eventuals in current $file.
git_repository_found=False

# Variables to help understand whether or not all updates
# (commit and shallow_since) are done.
is_commit_replaced=False
is_shallow_replaced=False

while IFS= read -r line; do
  # Check if current line of the file contains necessary name
  # of the git_repository which we gonna update with commit
  # and shallow_since.
  if [[ "${line}" =~ .*"name = \"com_github_3rdparty_eventuals\"".* ]];
  then
    git_repository_found=True
  fi

  # This is a right place for commit updates.
  if [[ "${line}" =~ .*"commit = ".* && $git_repository_found = True ]]; then
    new_commit_replace="commit = \"${commit}\","
    sed -i "s/$line/$new_commit_replace/" $file
    is_commit_replaced=True
  fi

  # This is a right place for shallow_since updates.
  if [[ "${line}" =~ .*"shallow_since = ".* && $git_repository_found = True ]]; then
    new_shallow_replace="shallow_since = \"${shallow_since}\","
    sed -i "s/$line/$new_shallow_replace/" $file
    is_shallow_replaced=True
  fi

  if [[ $is_commit_replaced = True && $is_shallow_replaced = True ]]; then
    break
  fi
done < "$file"

# Check for existence of buildifier.
which buildifier >/dev/null
if [[ $? != 0 ]]; then
  printf "Failed to find 'buildifier'\n"
  exit 1
fi

buildifier $file
