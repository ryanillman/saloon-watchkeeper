#pragma once
// Console commands, shared by the boards' USB console and the simulator's terminal.
typedef void (*app_out_fn)(const char *line);

// Runs one command (argv[0] is the command name). Returns 0 on success, 1 on a usage or
// config error. Call from the console task, never the UI task: it waits on the UI task.
int app_cmd_run(int argc, char **argv, app_out_fn out);
