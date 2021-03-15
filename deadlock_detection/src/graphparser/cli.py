import argparse

from parser_json import Parser as ParserJson
from parser_svg import make_parser as MakeParserSvg
from parser_proto import Parser as ParserProto

def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--log-files", nargs="+", help="these are the collected log files")
    #argp.add_argument("--log-files-proto", nargs="+", help="these are the collected protobuf log files")
    argp.add_argument("--input-type", action="store", default="json", help="choose input file format (json or proto)")
    argp.add_argument("--output", help="the output file", default="/dev/stdout")
    argp.add_argument("--svg", help="the output svg", default=None)
    args = argp.parse_args()

    if args.input_type == "proto":
        parser = (MakeParserSvg(ParserProto) if args.svg else ParserProto)()
        parser.add_files(args.log_files)
    else:
        parser = (MakeParserSvg(ParserJson) if args.svg else ParserJson)()
        files = []
        for logfile_name in args.log_files:
            with open(logfile_name) as logfile:
                files.append(list(logfile))
        parser.add_files(files)

    parser.output(args.output)

    if args.svg:
        parser._write_svg(args.svg)
