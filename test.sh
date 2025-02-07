#!/bin/bash
#
# kick_api.sh
#
# please set your host
address="http://[your management server]:8080"
# please set your api key
api_key="QVOObVBiTodKl5L0vPQFCCELsxbtEHHysXU42XTFFwhBHWWFV7GaiH5oJG0yHHsqFzCcgflH8Ee8Ttk4m_qJLQ"
# please set your secret key
secret_key="zTl4qQtSZPijMkNYgkX1lQh6QlEUXyjSKeXSPZQl2MJifzOBFhNDA5cQlfK7Ds5BFEM_ua_5ELMPL-z4JNJBIA"

api_path="/client/api?"

if [ $# -lt 1 ]; then
  echo "usage: $0 command=... paramter=... parameter=..."; exit;
elif [[ $1 != "command="* ]]; then
  echo "usage: $0 command=... paramter=... parameter=..."; exit;
elif [ $1 == "command=" ]; then
  echo "usage: $0 command=... paramter=... parameter=..."; exit;
fi

data_array=("$@" "apikey=${api_key}") 

temp1=$(echo -n ${data_array[@]} | \
   tr " " "\n" | \
   sort -fd -t'=' | \
   perl -pe's/([^-_.~A-Za-z0-9=\s])/sprintf("%%%02X", ord($1))/seg'| \
   tr "A-Z" "a-z" | \
   tr "\n" "&" )

signature=$(echo -n ${temp1[@]})
signature=${signature%&}
signature=$(echo -n $signature | \
   openssl sha1 -binary -hmac $secret_key | \
   openssl base64 )
signature=$(echo -n $signature | \
   perl -pe's/([^-_.~A-Za-z0-9])/sprintf("%%%02X", ord($1))/seg')

url=${address}${api_path}$(echo -n $@ | tr " " "&")"&"apikey=$api_key"&"signature=$signature

echo " SEND URL: $url"
curl ${url} | xmllint --format - 
