#! /bin/sh
#
# Usage: update-copyrights.sh "author" <files/dirs>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) version 3.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301  USA

currentyear=`date +%Y`
oldyear=`expr $currentyear - 1`
export currentyear oldyear author

addcopyright () {
    file="$1"
    author="$2"

    if grep -w "Copyright.*$currentyear.*$author" $file >/dev/null; then
        # done
        true
    elif grep -w "Copyright.*-$oldyear $author" $file >/dev/null; then
        # replace end year
        perl -pi -e 's/-$ENV{oldyear} $ENV{author}/-$ENV{currentyear} $ENV{author}/' $file
        echo updated: $author: $file
    elif grep -w "Copyright.*$oldyear $author" $file >/dev/null; then
        # add consecutive year
        perl -pi -e 's/$ENV{oldyear} $ENV{author}/$ENV{oldyear}-$ENV{currentyear} $ENV{author}/' $file
        echo updated: $author: $file
    elif grep -w "Copyright.*$author" $file >/dev/null; then
        # add separate year
        perl -pi -e 's/(Copyright.*) $ENV{author}/$1, $ENV{currentyear} $ENV{author}/' $file
        echo updated: $author: $file
    elif grep -w "Copyright" $file >/dev/null; then
        # add new line after the last copyright line
        # -i doesn't work with reading all lines?
        perl -e '$_ = join ("", <>); s/(.*)((^[ *#]*Copyright)[^\n]*)/$1$2\n$3 (C) $ENV{currentyear} $ENV{author}/ms; print;' \
            $file >$file.bak && mv $file.bak $file && echo added: $author: $file ||
            rm $file.bak && echo no copyright: $author: $file
    else
        echo skipped: $author: $file
    fi
}

for file in `git ls-files "$@"`; do
    git log --since=2009-01-01 --pretty='format:%ai: %an <%ae>' $file |
        grep ^$currentyear |
        sed -e 's/.*: //' |
        sort -u |
        while read author; do
        case $author in *intel.com*)
                author="Intel Corporation"
        esac
        addcopyright "$file" "$author"
    done
done
