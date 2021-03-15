# Seastar Deadlock Detection

In order to detect deadlocks:

1. Compile Seastar with custom flag, e.g. run the configure script like this:
```
./configure.py --mode=release --cflags "'-DSEASTAR_DEADLOCK_DETECTION=yes'"
```
2. Select an application to check for deadlocks. Compile it with `-DSEASTAR_DEADLOCK_DETECTION` flag.
3. Run the application. This will produce log files in the working directory, with names `deadlock_detection_graphdump.{}.json`, where `{}` is a thread ID.
4. Run the detection script `./detect_deadlock.sh "path/deadlock_detection_graphdump.*.json"`. Be careful to list all the generated files.
5. You will learn about any deadlocks detected.

In order to make a visualization of collected data, run
```
python src/graphparser --log-files "path/deadlock_detection_graphdump.*.json" --output /dev/null
```
and check out `/tmp/deadlock.svg`.
