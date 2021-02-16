#!/bin/bash -e

if [[ $PLATFORM == "Unix" ]]; then
    sudo apt-get update
    sudo apt-get install lcov -y --allow-unauthenticated
fi
