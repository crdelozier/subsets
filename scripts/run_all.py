import os
import re
import json 
import sys
import shutil
import subprocess
import gspread
from oauth2client.service_account import ServiceAccountCredentials

suites = ["test_real",
          "test_synth",
          "train_real_compilable",
          "train_real_simple_io",
          "train_synth_compilable",
          "train_synth_rich_io",
          "train_synth_simple_io",
          "valid_real",
          "valid_synth"]

total_files = 0
failed = 0
rownum = 3

#Converts each .zst file in a directory to a .jsonl file
def convert_to_jsonl(directory):
        for filename in os.listdir(directory):
                # ADD CODE? (for .tar.gz files)
                #if filename.endswith('.tar.gz'):
                #tar_file = os.path.join(directory, filename)
                if filename.endswith('.jsonl.zst'):
                        zst_file = os.path.join(directory, filename)
                        jsonl_file = os.path.join(directory, filename.replace('.jsonl.zst', '.jsonl'))
                        command = f"zstdcat {zst_file} > {jsonl_file}"
                        os.system(command)
                        os.remove(zst_file) #Removes the original ZST file

#Parses each JSONL file (in each line per file, extracts C++ code and creates a new cpp file) in the given directory
def parse_directory(directory, cpp_directory, suite):
        global rownum
        filenum = 1
        for filename in os.listdir(directory):
                if filename.endswith('.jsonl'):
                        parse_jsonl_file(directory, filename, cpp_directory, filenum, suite)
                        filenum = filenum + 1 
                        rownum = rownum + 1

#Taking a single jsonl file, this function extracts C++ code from each line and stores this in a new cpp file.
def parse_jsonl_file(directory, jsonl_file, cpp_directory, filenum, suite):
        jsonl_filepath = directory + "/" + jsonl_file
        fileline = 1
        final_list = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        with open(jsonl_filepath, 'r') as file:
                for line in file:
                        json_obj = json.loads(line)
                        cpp_code = json_obj['text']
                        output_list = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                        
                        #Names the file
                        filename = "file" + str(filenum) + "line" + str(fileline) + ".cpp"
                        fileline = fileline + 1

                        #Calls function to save C++ file containing code from SPECIFIC KEYS
                        output_list = save_as_cpp_file(cpp_directory, filename, cpp_code['func_def'], cpp_code['real_exe_wrapper'], cpp_code['real_deps'], cpp_code['angha_deps'])
                        final_list = [x + y for x, y in zip(output_list, final_list)]
                       
        #Calls function to store all data from JSONL file
        stores_data_in_sheet(final_list, filenum,suite)
        #print(final_list, rownum)

#Stores as new cpp file
def save_as_cpp_file(cpp_directory, filename, code1, code2, code3, code4):
        #code2 = code2.replace('#include "/tmp/pytmpfile_2d0c40262005_42oivt8yns.c"\n', "")
        filepath = os.path.join(cpp_directory, filename)
        
        start_line = code1.count('\n')
        
        end_line = start_line + code1.count('\n')
        
        with open(filepath, 'w') as file:
                file.write("// real_deps \n")
                if code3:
                        file.write(code3)
                file.write("\n// func_def \n")
                if code1:
                        file.write(code1)
                file.write("\n // angha_deps? \n")
                #Delete below?
                #file.write(str(code4))
                file.write("\n// real_exe_wrapper \n")
                if code2:
                        file.write(code2)
                file.close()
        #Removes bad code
        with open(filepath, 'r') as file:
                lines = file.readlines()
        #Removes all lines with '/tmp'
        filtered_lines = [line for line in lines if not any(keyword in line for keyword in ["/tmp", "None", "# 1"])]
        with open(filepath, 'w') as file:
                file.writelines(filtered_lines)
                file.close()

        #Runs tool
        #print("\nruns tool-function on ", filename)
        first_run = True
        return run_ironclad_scan_csv(filepath, first_run, start_line, end_line)



#Runs tool on .cpp file
def run_ironclad_scan_csv(cpp_filepath, first_run, start_line, end_line):
        global failed
        global total_files
        output_list = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

        # Executes command, captures and parses output as a list of integers. Also handles possible exceptions.

        #Attempts to compile cpp file, handles some errors--may need to handle more errors
        filepath = "home/ladmin/Downloads/" + cpp_filepath
        filepath = "/home/ladmin/Downloads/" + str(cpp_filepath)
        completed_process = subprocess.Popen(['g++', filepath, '-o', "program"], stderr=subprocess.PIPE)
        _, error_output = completed_process.communicate()
        #completed_process = subprocess.run(['g++', str(filepath), '-o', "program"], shell=True, capture_output=True, text=True)
        #print(os.getcwd())

        if not completed_process.returncode == 0:
                if first_run:
                        print("ERROR")
                        first_run = False
                        #print(str(e))
                        e = error_output
                        #print(e.decode())

                        # Adds '__bench' if needed
                        if str(e).find("undeclared identifier") != -1 or str(e).find("conflicting declaration") != -1:
                                #print("use of undeclared identifier or conflicting declaration error found, adding '__bench'")
                                with open(cpp_filepath, 'r') as file:
                                        code = file.read()
                                index_of_openpar = code.index("(")
                                modified_code = code[:index_of_openpar] + "__bench" + code[index_of_openpar:]
                                with open(cpp_filepath, 'w') as file:
                                        file.write(modified_code)
                                        file.close()
                                # Reruns function with '__bench' added, first_run is false
                                run_ironclad_scan_csv(cpp_filepath, first_run, start_line, end_line)
                elif not first_run:
                        failed = failed + 1
                        total_files = total_files + 1
                        return output_list
                        #print("Other errors found, please check")

        #Runs tool
        command = "./ironclad-scan-csv --start_line=" + str(start_line) + " --end_line=" + str(end_line) + " " + cpp_filepath + " -- -I/usr/lib/gcc/x86_64-linux-gnu/9/include/"
        try:
                #subprocess.run(command, shell=True)   # Runs command
                #print("runs ironclad-scan-csv ")
                output = subprocess.check_output(command, shell=True, text=True) 
                output_list = [ int(num) for num in output.strip().split(",")]
                #return output_list
        except Exception as e:
                #print("Errors while running tool")
                return output_list

        total_files = total_files + 1
        return output_list

#Stores data (from a single JSONL file) in google spreadsheet
def stores_data_in_sheet(final_list, filenum,suite):
        filename = "file" + str(filenum) + ".jsonl"
        values = [suite, filename] + final_list + [failed,total_files]
        #leaves cells in A and B blank, starts list from cell C
        #worksheet.insert_row([None] *2 + final_list, index=rownum)
        worksheet.insert_row(values, rownum)
        #print("updates sheet for ", filename)

# Authenticate and authorize the Google Sheets API
scope = ["https://spreadsheets.google.com/feeds", "https://www.googleapis.com/auth/drive"]
credentials = ServiceAccountCredentials.from_json_keyfile_name("/home/ladmin/Downloads/exebench-and-ironclad-dc6f06e9506b.json", scope) 
client = gspread.authorize(credentials)

# Open the specified Google Sheet and get worksheet
sheet = client.open("Exebench + Ironclad Sheet")
worksheet = sheet.worksheet('Stats')

for suite in suites:
        directory_path = suite

        total_files = 0
        failed = 0

        #Get directory for CPP files and create new directory, if necessary
        cpp_directory = "cpp_" + suite
        if not os.path.exists(cpp_directory):
                os.makedirs(cpp_directory)

        #Converts all files to JSONL files
        convert_to_jsonl(directory_path)

        #Parses all JSONL files in a directory
        parse_directory(directory_path, cpp_directory,suite)

        rownum = rownum + 1
