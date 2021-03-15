import parser
import graph as gr
import os
import argparse
from distutils.util import strtobool


def test(test_file, output_file):
    with open(test_file) as testfile:
        file = testfile.readlines()

    with open(output_file) as output_file:
        a = output_file.readline()
        output = strtobool(a[:-1])

    graph_parser = parser.GraphParser(file)

    graph, semaphores = graph_parser.build_graph()
    res, execution = gr.Graph(graph, semaphores).is_deadlock_free()

    if res != output:
        print("\u001b[31m" + "The answer is incorrect \u001b[0m\n")
    else:
        print("\u001b[32m" + "The answer is correct \u001b[0m\n")


def all_files_sorted(dir):
    files = list(map(lambda a: dir + "/" + a, os.listdir(dir)))
    files.sort()
    return files


if __name__ == "__main__":
    argp = argparse.ArgumentParser()
    argp.add_argument("-dir", nargs=2, metavar=('tests', 'outputs'), help="directory of tests and directory of outputs",
                      default=["./tests", "./outputs"])
    args = argp.parse_args()

    test_files = all_files_sorted(args.dir[0])
    output_files = all_files_sorted(args.dir[1])

    for i in range(len(test_files)):
        print(test_files[i])
        test(test_files[i], output_files[i])
