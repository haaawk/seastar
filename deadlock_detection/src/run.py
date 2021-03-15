import parser
import graph as gr
import json
import argparse
import sys

if __name__ == "__main__":
    argp = argparse.ArgumentParser()
    argp.add_argument("-file", help="the file to check for deadlock and print the result", default=None)
    argp.add_argument("-verbose_deadlock", help="Whether to print additional information about the found deadlock",
                      default=False)
    argp.add_argument("-debug_deadlock", help="Whether to print debug information about the found deadlock",
                      default=False)
    argp.add_argument("-debug_deadlock_file", help="Path of the file to which write debug information, if no is "
                                                   "provided stderr will be used", default=None)
    args = argp.parse_args()

    usage_message = "Usage: python run.py -file file_path [-debug_deadlock | -verbose_deadlock [-debug_deadlock_file " \
                    "file_path]] "

    if args.file is None:
        print(usage_message)
        sys.exit(1)
    else:
        with open(args.file) as testfile:
            file = list(testfile)

        graphParser = parser.GraphParser(file)
        graph, semaphores = graphParser.build_graph()

        result, execution = gr.Graph(graph, semaphores).is_deadlock_free()

        if result:
            print("No deadlock was found")
            exit(0)

        print("Deadlock was found")
        print("\n")
        if args.verbose_deadlock:
            print(execution)

        if args.debug_deadlock:
            debug_info = execution.get_debug_info()
            if args.debug_deadlock_file is None:
                for semaphore in debug_info:
                    print(json.dumps(semaphore))
            else:
                with open(args.debug_deadlock_file, "w") as debug_file:
                    for semaphore in debug_info:
                        debug_file.write(json.dumps(semaphore))

    exit(1)
