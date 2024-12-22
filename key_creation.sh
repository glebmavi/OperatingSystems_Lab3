#!/bin/bash

openssl req -x509 -new -nodes -utf8 -sha256 -days 36500 \
-batch -config x509.genkey -outform DER \
-out signing_key.x509 \
-keyout signing_key.priv