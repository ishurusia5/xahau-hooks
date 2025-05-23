#!/bin/bash -u

# Generate the SetHook_wasm.h file from the SetHook_test.cpp file.
# 
# Prerequisites:
# - wasmcc:
#   https://github.com/wasienv/wasienv
#
# - hook-cleaner:
#   https://github.com/RichardAH/hook-cleaner-c
#
# - wat2wasm
#   https://github.com/WebAssembly/wabt
#
# - clang-format:
#   Ubuntu: $sudo apt-get install clang-format
#   macOS: $brew install clang-format
#
# - (macOS Only) GNU sed, grep:
#   $brew install gnu-sed grep
#   add path: PATH="/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH"

set -e
# Get the script directory (retrieving the correct path regardless of where it's executed from)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
# Set the project root directory
WASM_DIR="generated/hook/c"
INPUT_FILE="SetHook_test.cpp"
OUTPUT_FILE="SetHook_wasm.h"

mkdir -p $WASM_DIR
echo '
//This file is generated by build_test_hooks.h
#ifndef SETHOOK_WASM_INCLUDED
#define SETHOOK_WASM_INCLUDED
#include <map>
#include <stdint.h>
#include <string>
#include <vector>
namespace ripple {
namespace test {
std::map<std::string, std::vector<uint8_t>> wasm = {' > $OUTPUT_FILE
COUNTER="0"
cat $INPUT_FILE | tr '\n' '\f' | 
        grep -Po 'R"\[test\.hook\](.*?)\[test\.hook\]"' | 
        sed -E 's/R"\[test\.hook\]\(//g' | 
        sed -E 's/\)\[test\.hook\]"[\f \t]*/\/*end*\//g' | 
        while read -r line
        do
            echo "/* ==== WASM: $COUNTER ==== */" >> $OUTPUT_FILE
            echo -n '{ R"[test.hook](' >> $OUTPUT_FILE
            cat <<< "$line" | sed -E 's/.{7}$//g' | tr -d '\n' | tr '\f' '\n' >> $OUTPUT_FILE
            echo ')[test.hook]",' >> $OUTPUT_FILE
            echo "{" >> $OUTPUT_FILE
            WAT=`grep -Eo '\(module' <<< $line | wc -l`
            if [ "$WAT" -eq "0" ]
            then
                echo '#include "api.h"' > "$WASM_DIR/test-$COUNTER-gen.c"
                tr '\f' '\n' <<< $line >> "$WASM_DIR/test-$COUNTER-gen.c"
                DECLARED="`tr '\f' '\n' <<< $line | grep  -E '(extern|define) ' | grep -Eo '[a-z\-\_]+ *\(' | grep -v 'sizeof' | sed -E 's/[^a-z\-\_]//g' | sort | uniq`"
                USED="`tr '\f' '\n' <<< $line | grep -vE '(extern|define) ' | grep -Eo '[a-z\-\_]+\(' | grep -v 'sizeof' | sed -E 's/[^a-z\-\_]//g' | grep -vE '^(hook|cbak)' | sort | uniq`"
                ONCE="`echo $DECLARED $USED | tr ' ' '\n' | sort | uniq -c | grep '1 ' | sed -E 's/^ *1 //g'`"
                FILTER="`echo $DECLARED | tr ' ' '|' | sed -E 's/\|$//g'`"
                UNDECL="`echo $ONCE | grep -v -E $FILTER 2>/dev/null || echo ''`"
                if [ ! -z "$UNDECL" ]
                then
                    echo "Undeclared in $COUNTER: $UNDECL"
                    echo "$line"
                    exit 1
                fi
                wasmcc -x c /dev/stdin -o /dev/stdout -O2 -Wl,--allow-undefined <<< "`tr '\f' '\n' <<< $line`" |
                    hook-cleaner - - 2>/dev/null |
                    xxd -p -u -c 10 | 
                    sed -E 's/../0x&U,/g' | sed -E 's/^/    /g' >> $OUTPUT_FILE
            else
                wat2wasm - -o /dev/stdout <<< "`tr '\f' '\n' <<< $(sed -E 's/.{7}$//g' <<< $line)`" |
                    xxd -p -u -c 10 | 
                    sed -E 's/../0x&U,/g' | sed -E 's/^/    /g' >> $OUTPUT_FILE
            fi
            if [ "$?" -gt "0" ]
            then
                echo "Compilation error ^"
                exit 1
            fi
            echo '}},' >> $OUTPUT_FILE
            echo >> $OUTPUT_FILE
            COUNTER=`echo $COUNTER + 1 | bc`
        done
echo '};
}
}
#endif' >> $OUTPUT_FILE
clang-format -i $OUTPUT_FILE
