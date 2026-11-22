#!/bin/sh
# Tester script for assignment 1, assignment 2, and assignment 4

set -e
set -u

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data

CONFIG_DIR=/etc/finder-app/conf
RESULT_FILE=/tmp/assignment4-result.txt

username=$(cat "$CONFIG_DIR/username.txt")

if [ "$#" -lt 3 ]
then
    echo "Using default value ${WRITESTR} for string to write"
    if [ "$#" -lt 1 ]
    then
        echo "Using default value ${NUMFILES} for number of files to write"
    else
        NUMFILES=$1
    fi
else
    NUMFILES=$1
    WRITESTR=$2
    WRITEDIR=/tmp/aeld-data/$3
fi

MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

rm -rf "$WRITEDIR"

assignment=$(cat "$CONFIG_DIR/assignment.txt")

if [ "$assignment" != 'assignment1' ]
then
    mkdir -p "$WRITEDIR"

    if [ -d "$WRITEDIR" ]
    then
        echo "$WRITEDIR created"
    else
        exit 1
    fi
fi

echo "Writing files using the installed writer utility"

for i in $(seq 1 "$NUMFILES")
do
    writer "$WRITEDIR/${username}$i.txt" "$WRITESTR"
done

finder.sh "$WRITEDIR" "$WRITESTR" > "$RESULT_FILE"

rm -rf /tmp/aeld-data

if grep -Fq "$MATCHSTR" "$RESULT_FILE"
then
    echo "success"
    exit 0
else
    echo "failed: expected ${MATCHSTR} in ${RESULT_FILE}"
    cat "$RESULT_FILE"
    exit 1
fi
