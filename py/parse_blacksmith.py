#!/usr/bin/python3
"""! @brief Python program for parsing the output of the Blacksmith Rowhammer
            fuzzer to use with gluezilla-templater."""

import json
import argparse
import pandas as pd


def get_best_pattern(data):
    """! Determines the best pattern by bit flip count.

    @param data The data from json file.

    @return The id and mapping id of the pattern with most bit flips.
    """

    total_bitflips_old = 0
    id = 0
    mapping = 0
    df = pd.DataFrame(data["hammering_patterns"])
    # determine best mapping
    for i in range(0, len(df["address_mappings"])):
        for j in range(3):
            total_bitflips = 0
            for k in range(3):
                try:
                    total_bitflips += len(data["hammering_patterns"]
                                          [i]["address_mappings"]
                                          [j]["bit_flips"][k])
                except:  # cos the data structure is sometimes strange
                    pass
            if total_bitflips_old < total_bitflips:
                total_bitflips_old = total_bitflips
                id = i
                mapping = j

    return id, mapping


def create_hammer_order(access_ids_list):
    """! Constructs the hammer order configuration string

    @param  access_ids_list The access_ids_list of the blacksmith pattern.

    @return The configuration string.
    """

    vector = "hammer_order="
    for id in access_ids_list:
        vector += f"{id},"

    vector = vector[:-1]

    return vector


def code_jitter_to_config(code_jitter):
    """! Constructs a string with the configuration needed from the code_jitter
         entry.

    @param code_jitter The code_jitter of the blacksmith pattern.

    @return The configuration string.
    """

    variables = ""
    variables += f"num_aggs_for_sync={code_jitter['num_aggs_for_sync']}\n"
    variables += f"total_num_activations={code_jitter['total_activations']}\n"
    variables += f"fencing={str(code_jitter['fencing_strategy']).lower()}\n"
    variables += f"flushing={str(code_jitter['flushing_strategy']).lower()}\n"

    return variables


def output_best_pattern(data, filename):
    """! Outputs the best pattern to file.

    @param data The data from json file.
    @param filename The filename of the Blacksmith json file used for
           generating the best pattern file.
    """

    df = pd.DataFrame(data["hammering_patterns"])
    # get id of best pattern
    id, mapping = get_best_pattern(data)
    # get access ids as list from dataframe
    access_ids = list(df.iloc[id, 0])
    vec = create_hammer_order(access_ids)
    # get best pattern from dataframe
    df_best_pattern = df.iloc[id, 1]
    var = code_jitter_to_config(df_best_pattern[mapping]["code_jitter"])
    # create config string
    config_string = f"{vec}\n{var}"
    # write config to file
    with open(filename + "_best_pattern.txt", "w") as config_file:
        config_file.write(config_string)


def output_all_patterns(data, filename, min_bitflips, max_aggs):
    """! Outputs all patterns with bitflips > min_bitflips
         and aggressors < max_aggs

    @param  data The data from json file.
    """

    df = pd.DataFrame(data["hammering_patterns"])
    # clear output file if exists
    open(filename + "_all_patterns.txt", "w").close()
    # count bit flips achieved by pattern
    for i in range(len(df)):
        total_bitflips = 0
        for j in range(3):
            for k in range(3):
                try:
                    total_bitflips += len(data["hammering_patterns"]
                                          [i]["address_mappings"]
                                          [j]["bit_flips"][k])
                except:  # cos the data structure is sometimes strange
                    pass
        # get access ids as list from dataframe
        access_ids = list(df.iloc[i, 0])
        # if too many agg.s -> continue
        if max(access_ids) > max_aggs:
            continue
        vec = create_hammer_order(access_ids)
        # get pattern from dataframe
        df_pattern = df.iloc[i, 1]
        var = code_jitter_to_config(df_pattern[0]["code_jitter"])
        # create config string
        config_string = f"ID: {i}\nBitflips: {total_bitflips}\n{vec}\n{var}\n"
        # write config to file
        if total_bitflips >= min_bitflips:
            with open(filename + "_all_patterns.txt", "a") as config_file:
                config_file.write(config_string)


def main():
    """! Main program entry."""

    # parse arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('filename')
    parser.add_argument(
        '--min_bitflips', dest='min_bitflips', default=100, type=int)
    parser.add_argument('--max_aggs', dest='max_aggs', default=20, type=int)
    args = parser.parse_args()
    filename = args.filename
    min_bitflips = args.min_bitflips
    max_aggs = args.max_aggs
    # read the json file
    with open(filename, 'r') as file:
        data = json.load(file)
    # output the pattern with the most bit flips to file
    output_best_pattern(data, filename)
    # output all patterns with bitflips > min_bitflips to file
    output_all_patterns(data, filename, min_bitflips, max_aggs)


if __name__ == "__main__":
    main()


# json format

""" hammering_patterns
    Index 0 = access_ids
    Index 1 = address_mappings
    Index 2 = agg_access_patterns
    Index 3 = base_period
    Index 4 = id
    Index 5 = is_location_dependent
    Index 6 = max_period
    Index 7 = num_refresh_intervals
    Index 8 = total_activations
"""

""" address_mappings
    Index 0 = aggressor_to_addr1
    Index 1 = aggressor_to_addr2
    Index 2 = aggressor_to_addr3
"""

""" aggressor_to_addr -> dict
    "aggressor_to_addr"
    "bank_no"
    "bit_flips"
    "code_jitter"
    "id"
    "max_row"
    "min_row"
    "reproducibility_score"

"""

""" code_jitter -> dict
    "fencing_strategy"
    "flushing_strategy"
    "num_aggs_for_sync"
    "pattern_sync_each_ref"
    "total_activations"
"""
