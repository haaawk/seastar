import parser
import graph as gr
import os
import argparse
from distutils.util import strtobool

def test(test_file, output_file):
    with open(test_file) as testfile:
        file = list(testfile)

    with open(output_file) as output_file:
        a = output_file.readline()
        output = strtobool(a[:-1])

    graph_parser = parser.GraphParser(file)

    graph, semaphores = graph_parser.build_graph()

    if gr.Graph(graph, semaphores).is_deadlock_free() != output:
        print(test_file)


def all_files_sorted(dir):
    files = list(map(lambda a: dir + "/" + a, os.listdir(dir)))
    files.sort()
    return files


if __name__ == "__main__":
    argp = argparse.ArgumentParser()
    argp.add_argument("-dir", nargs=2, metavar=('tests', 'outputs'), help="directory of tests and directory of outputs",
                      default=["./tests", "./outputs"])
    argp.add_argument("-file", help="the file to check for deadlock and print the result", default=None)
    args = argp.parse_args()

    if args.file is None:
        test_files = all_files_sorted(args.dir[0])
        output_files = all_files_sorted(args.dir[1])

        for i in range(len(test_files)):
            print(test_files[i])
            test(test_files[i], output_files[i])
    else:
        with open(args.file) as testfile:
            file = list(testfile)

        graphParser = parser.GraphParser(file)
        graph, semaphores = graphParser.build_graph()

        print(gr.Graph(graph, semaphores).is_deadlock_free())
