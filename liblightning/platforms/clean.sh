#!/bin/bash

cd $(dirname $0)

find . -name "*.o" -exec rm '{}' ';'
find . -name "*.a" -exec rm '{}' ';'
