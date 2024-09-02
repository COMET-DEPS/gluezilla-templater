#!/usr/bin/python3
"""! @brief Python program for testing all Blacksmith Patterns from a file."""

import re
import configparser
import signal
import subprocess
import sys
import os
import time
import argparse

# regex for parsing the patterns
id_regex = "ID: (\d*)"
bitflips_regex = "Bitflips: (\d*)"
hammer_order_regex = "hammer_order=([\d,\d]*)"
num_aggs_for_sync_regex = "num_aggs_for_sync=(\d*)"
total_num_activations_regex = "total_num_activations=(\d*)"
fencing_regex = "fencing=(.*)"
flushing_regex = "flushing=(.*)"


def run_gluezilla_templater():
    """! Executes gluezilla-templater.

    """

    global process_pid
    process = subprocess.Popen(['./bin/tester'], stdout=subprocess.PIPE,
                            universal_newlines=True, shell=True)
    process_pid = process.pid
    while True:
        output = process.stdout.readline()
        print(output.strip())     
        return_code = process.poll()
        if return_code is not None:
            print('RETURN CODE', return_code)
            # process has finished, read rest of the output 
            for output in process.stdout.readlines():
                print(output.strip())
            break


def signal_handler(sig, frame):
    """! Enable pressing Ctrl+C on gluezilla-templater.
    
    """

    print('You pressed Ctrl+C!')
    os.kill(process_pid, signal.SIGINT)
    print('Waiting 30 seconds for DB to finish!')
    time.sleep(30)
    sys.exit(0)


def test_patterns(filename):
    """! Test the patterns from specified file.

    @params filename The filename of the file containing the Blacksmiths
            patterns.
    """

    config = configparser.ConfigParser()
    with open(filename, 'r') as bsp_file:
        line = bsp_file.readline()
        while line:       
            x = re.search(id_regex, line)
            if x:
                # get pattern from file
                id = x.group(1)
                line = bsp_file.readline()
                x = re.search(bitflips_regex, line)
                bitflips = x.group(1)
                line = bsp_file.readline()
                x = re.search(hammer_order_regex, line)
                hammer_order = x.group(1)
                line = bsp_file.readline()
                x = re.search(num_aggs_for_sync_regex, line)
                num_aggs_for_sync = x.group(1)
                line = bsp_file.readline()
                x = re.search(total_num_activations_regex, line)
                total_num_activations = x.group(1)
                line = bsp_file.readline()
                x = re.search(fencing_regex, line)
                fencing = x.group(1)
                line = bsp_file.readline()
                x = re.search(flushing_regex, line)
                flushing = x.group(1) 
            # adjust configuration
            config.read('./config.ini')
            config.set("blacksmith", "hammer_order", str(hammer_order))
            config.set("blacksmith", "num_aggs_for_sync", str(num_aggs_for_sync))
            config.set("blacksmith", "total_num_activations", str(total_num_activations))
            config.set("blacksmith", "fencing", fencing)
            config.set("blacksmith", "flushing", flushing)
            config.set("db.experiments", "comment", "ID:" + str(id))
            # write config.ini
            with open('./config.ini', 'w') as configfile:
                config.write(configfile, space_around_delimiters=False)
            # execute gluezilla-templater
            run_gluezilla_templater()
            line = bsp_file.readline()


def main():
    """! Main program entry."""

    # parse arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('filename')
    args = parser.parse_args()
    filename = args.filename
    # signal handler to enable stopping gluezilla-templater
    signal.signal(signal.SIGINT, signal_handler)
    test_patterns(filename)


if __name__ == "__main__":
    main()

