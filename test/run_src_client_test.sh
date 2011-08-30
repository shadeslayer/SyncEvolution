#!/bin/sh

# This script is run by `make check'. Since `make check' is run from top source
# directory and `client-test' is expected to be run from `src' directory, this
# script have to be employed, so `client-test' can find some files.
cd 'src' && ./client-test
