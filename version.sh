#!/bin/sh
# using --tags might fix the git desribe, but honestly maybe inho just didn't even need it to succeed? TODO
if git describe | sed s/-/_/g > version.num.tmp
then
    mv version.num.tmp version.num
    echo "m4_define([VERSION_NUMBER], [`tr -d '\n' < version.num`])" \
        > version.m4
    sed s/@VERSION@/`cat version.num`/ < memcached.spec.in > memcached.spec
else
    rm version.num.tmp
fi
