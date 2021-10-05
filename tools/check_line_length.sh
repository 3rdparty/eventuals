 #!/bin/bash
 
 # This function takes the file name as a first parameter ($1) and checks if 
 # any line has more than 80 characters.
 check_line_length(){
    status=0
    local -i number=1
    while IFS= read -r line; do
      local -i length=${#line}
      if (( length > 80 )); then
        status=1
        tput bold
        printf "%s:%i:%i: " "$1" "${number}" "${length}"
        tput setaf 1 # Red.
        printf "error: "
        tput sgr0 # Reset terminal.
        tput bold
        printf "line exceeds 80 characters\n"
        tput sgr0 # Reset terminal.
        printf "%s\n" "${line}"
      fi
      (( number++ ))
    done < "$1"
    return ${status}
 }
