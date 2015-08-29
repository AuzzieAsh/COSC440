#!/bin/bash
cat asgn1.c > /dev/asgn1
cat /dev/asgn1 > output.txt
diff asgn1.c output.txt
