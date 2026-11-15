#!/bin/sh

filesdir=$1
searchstr=$2

echo "The number of passed arguments: $#"

if [ $# -lt 2 ]; then
    echo "Missing argument, the number of arguments should be 2"
    exit 1
fi

if [ -d "$filesdir" ]; then
    echo "Valid directory"

    find "$filesdir" -type f ! -name "searchResults.txt" ! -name "FilesCount.txt" -exec grep -l "$searchstr" {} \; > FilesCount.txt
    X=$(wc -l < FilesCount.txt)

    find "$filesdir" -type f ! -name "searchResults.txt" ! -name "FilesCount.txt" -exec grep "$searchstr" {} \; > searchResults.txt
    Y=$(wc -l < searchResults.txt)

    echo "The number of files are $X and the number of matching lines are $Y"
else
    echo "not a valid directory"
    exit 1
fi

exit 0
