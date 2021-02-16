#!/bin/bash -e

if [[ "$COVERAGE" == "1" && "$TRAVIS_EVENT_TYPE" != "pull_request" ]]; then
    cd build/
    gem install coveralls-lcov
    lcov -c -d CMakeFiles/rules.dir -d CMakeFiles/start.dir -o coverage.info

    lcov -r coverage.info '/usr/include/*' -o coverage-filtered.info

    lcov --list coverage-filtered.info

    coveralls-lcov --repo-token ${COVERALLS_TOKEN} coverage.info
fi
