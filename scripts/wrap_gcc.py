#!/usr/bin/python

import sys,os,subprocess

args = sys.argv[1:]
args_minus_source = []
source = ""

OUTPUT_PATH = "TODO"
TOOL_PATH = "TODO"
SYSTEM_INCLUDES = "-I/usr/lib/gcc/x86_64-linux-gnu/7/include/ -I/usr/lib/gcc/x86_64-linux-gnu/7/include-fixed/"

output_file = open(OUTPUT_PATH,"a")

for arg in args:
    if arg.lower().endswith(".c") or arg.lower().endswith(".cpp") or arg.lower().endswith(".cc") or arg.lower().endswith(".cxx"):
        source = arg
    else:
        args_minus_source.append(arg)

try:
    output = subprocess.check_output(TOOL_PATH + " " + source + " -- " + " ".join(args_minus_source) + " " + SYSTEM_INCLUDES, shell=True)
    output_file.write(output + "\n")
except Exception as e:
    print("Error with scan tool: ")
    print(e)

output_file.close()

os.system("gcc " + " ".join(args))
