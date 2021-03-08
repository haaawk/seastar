import argparse
from pathlib import PurePath

from parser_json import Parser as ParserJson
#from parser_svg import Parser as ParserSvg

def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--log-files", nargs="+", help="these are the collected log files", required=True)
    argp.add_argument("--output", help="the output file", default=None)
    argp.add_argument("--svg", help="the output file", action="store_true", default=False)
    args = argp.parse_args()

    #assert len(args.log_files) == 1
    assert not args.svg

    output = args.output
    if output is None:
        input_file = PurePath(args.log_files[0])
        output = str(input_file.with_name(input_file.name + ".out.json"))

    parser = ParserJson()

    files = []
    for logfile_name in args.log_files:
        with open(logfile_name) as logfile:
            files.append(list(logfile))

    parser.add_files(files)

    parser.output(output)
