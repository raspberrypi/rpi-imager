@echo off

IF "%APPVEYOR_REPO_BRANCH%"=="master" (
  IF "%GITHUB_TOKEN%" NEQ "" (
    npm run prebuild-release -- -u %GITHUB_TOKEN%
  )
)
