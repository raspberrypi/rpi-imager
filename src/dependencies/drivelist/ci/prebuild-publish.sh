#!/bin/bash

if [[ $GITHUB_TOKEN ]]; then
  npm run prebuild-release -- -u "$GITHUB_TOKEN"
fi
