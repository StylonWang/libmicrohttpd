#!/bin/bash

MAX_FOLDER_DEPTH=5
FOLDER_DEPTH=$RANDOM%$MAX_FOLDER_DEPTH
MAX_FILES_PER_FOLDER=10

create_file ()
{
    local file_name=$1
    local parent_folder=$2
    local file_length=`expr $RANDOM \* 10`
    local log_file=/tmp/$RANDOM

    #echo "creating $file_name with $file_length bytes..."
    dd if=/dev/urandom of=$file_name bs=1 count=$file_length >$log_file 2>&1
    if (($? != 0)); then
        echo "Failed to create file: $file_name "
        cat $log_file
    fi
    rm $log_file
}

populate_folder ()
{
    local cwd=$1
    local depth=`expr $2 - 1`
    local c

    if (($depth == -1)); then
        return
    fi

    #echo "cwd=$cwd"
    #echo "depth=$depth"

    echo "creating folder $cwd level $depth ..."
    mkdir -p $cwd
    cd $cwd

    for ((c=0; c<$MAX_FILES_PER_FOLDER; c++))
    do

        # folder-to-file ratio is 3:7
        local ff_factor=$RANDOM%10
        # only create files, not folders in the last level
        if (($depth == 0)); then 
            ff_factor=9
        fi

        if (($ff_factor < 3)); then
            # randomly generate a folder name
            local folder_name="D"`date +%s`_$RANDOM
            # create a sub-folder
            populate_folder $folder_name $depth
        else 
            # randomly generate a file name
            local file_name="F"`date +%s`_$RANDOM
            create_file $file_name
        fi
    done

    echo "Generating MD5 checksum to md5sum.txt..."
    md5sum -b * >md5sum.txt
    cd ..
}

folder_name="D"`date +%s`_$RANDOM
populate_folder $folder_name $MAX_FOLDER_DEPTH

