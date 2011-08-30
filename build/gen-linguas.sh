#!/bin/sh -e

lings_new="LINGUAS.new.$$"
lings="LINGUAS"

# create LINGUAS file: every .po is included
cd po
ls -1 *.po | sort -u | sed -e 's/.po$//' >"$lings_new"

if test -f "$lings" && cmp -s "$lings" "$lings_new"
then
    rm "$lings_new"
else
    mv "$lings_new" "$lings"
fi
