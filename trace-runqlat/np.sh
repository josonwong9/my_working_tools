#!/bin/bash

if [ ! $# -eq 2 ]; then
        echo "Usage: sh np.sh nr_commit branch"
        echo "For example push 4 commit to stable branch"
        echo "sh np.sh 4 stable"
        exit
fi

n=$1
branch=$2
rm -rf upload
mkdir upload

git format-patch -$n -o upload

git reset --hard HEAD~$n

for ((i=1;i<=$n;i++))
do
        index=`printf "%04d" $i`
        p=`ls upload | grep $index`
        git am upload/$p
        if [ -n "$branch" ];then
                git $branch
        else
                git master
        fi
done
