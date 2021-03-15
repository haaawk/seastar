#!/bin/bash

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

pid="$$"

output_file="${SCRIPT_DIR}/.tmp.simplified_data_${pid}.json"

python "${SCRIPT_DIR}/src/graphparser" --log-files "$@" --output "${output_file}" || exit 1

python "${SCRIPT_DIR}/src/run.py" -file "${output_file}" || exit 1

rm -f "${output_file}"
