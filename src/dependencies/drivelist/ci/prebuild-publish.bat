@echo off

if %1 == 10 (
  if %APPVEYOR_REPO_BRANCH% == master (
    if %GITHUB_TOKEN% neq "" (
      npm run prebuild-release -- -u %GITHUB_TOKEN%
    )
  )
)
