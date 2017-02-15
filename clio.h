// -------------------------------------------------------------------------
// Clio: a minimalist argument-parsing library designed for building elegant
// command line interfaces.
//
// Clio is written in portable C99.
//
// Author: Darren Mulholland <darren@mulholland.xyz>
// License: Public Domain
// Version: 2.0.0
// -------------------------------------------------------------------------

#pragma once
#include <stdbool.h>


// -------------------------------------------------------------------------
// Types.
// -------------------------------------------------------------------------

// An ArgParser instance is responsible for storing registered options and
// commands. Note that every registered command recursively receives an
// ArgParser instance of its own.
typedef struct ArgParser ArgParser;


// -------------------------------------------------------------------------
// ArgParser initialization and teardown.
// -------------------------------------------------------------------------

// Initialize a new ArgParser instance. Supplying help text activates the
// automatic --help flag, supplying a version string activates the automatic
// --version flag. NULL can be passed for either parameter.
ArgParser* ap_new(char *helptext, char *version);

// Free the memory associated with an ArgParser instance.
void ap_free(ArgParser *parser);


// -------------------------------------------------------------------------
// Parsing arguments.
// -------------------------------------------------------------------------

// Parse an array of string arguments. Note that the function's parameters
// are assumed to be argc and argv as supplied to main(), i.e. the first
// element of the array is assumed to be the program name and ignored.
void ap_parse(ArgParser *parser, int argc, char **argv);


// -------------------------------------------------------------------------
// Registering options.
// -------------------------------------------------------------------------

// Register a boolean option.
void ap_add_flag(ArgParser *parser, char *name);

// Register a string option with a default value.
void ap_add_str(ArgParser *parser, char *name, char* value);

// Register an integer option with a default value.
void ap_add_int(ArgParser *parser, char *name, int value);

// Register a floating-point option with a default value.
void ap_add_float(ArgParser *parser, char *name, double value);

// Register a boolean list option.
void ap_add_flag_list(ArgParser *parser, char *name);

// Register a string list option.
void ap_add_str_list(ArgParser *parser, char *name, bool greedy);

// Register an integer list option.
void ap_add_int_list(ArgParser *parser, char *name, bool greedy);

// Register a floating-point list option.
void ap_add_float_list(ArgParser *parser, char *name, bool greedy);


// -------------------------------------------------------------------------
// Retrieving option values.
// -------------------------------------------------------------------------

// Returns true if the specified option was found while parsing.
bool ap_found(ArgParser *parser, char *name);

// Returns the value of a boolean option.
bool ap_get_flag(ArgParser *parser, char *name);

// Returns the value of a string option.
char* ap_get_str(ArgParser *parser, char *name);

// Returns the value of an integer option.
int ap_get_int(ArgParser *parser, char *name);

// Returns the value of a floating-point option.
double ap_get_float(ArgParser *parser, char *name);

// Returns the length of a list-option's list of values.
int ap_len_list(ArgParser *parser, char *name);

// Returns a list-option's values as a freshly-allocated array of booleans.
// The array's memory is not affected by calls to ap_free().
bool* ap_get_flag_list(ArgParser *parser, char *name);

// Returns a list-option's values as a freshly-allocated array of string
// pointers. The array's memory is not affected by calls to ap_free().
char** ap_get_str_list(ArgParser *parser, char *name);

// Returns a list-option's values as a freshly-allocated array of integers.
// The array's memory is not affected by calls to ap_free().
int* ap_get_int_list(ArgParser *parser, char *name);

// Returns a list-option's values as a freshly-allocated array of doubles.
// The array's memory is not affected by calls to ap_free().
double* ap_get_float_list(ArgParser *parser, char *name);


// -------------------------------------------------------------------------
// Setting option values.
// -------------------------------------------------------------------------

// Clear the specified option's internal list of values.
void ap_clear_list(ArgParser *parser, char *name);

// Append a value to a boolean option's internal list.
void ap_set_flag(ArgParser *parser, char *name, bool value);

// Append a value to a string option's internal list.
void ap_set_str(ArgParser *parser, char *name, char *value);

// Append a value to an integer option's internal list.
void ap_set_int(ArgParser *parser, char *name, int value);

// Append a value to a floating-point option's internal list.
void ap_set_float(ArgParser *parser, char *name, double value);


// -------------------------------------------------------------------------
// Positional arguments.
// -------------------------------------------------------------------------

// Returns true if the parser has found one or more positional arguments.
bool ap_has_args(ArgParser *parser);

// Returns the number of positional arguments.
int ap_len_args(ArgParser *parser);

// Returns the positional argument at the specified index.
char* ap_get_arg(ArgParser *parser, int index);

// Returns the positional arguments as a freshly-allocated array of string
// pointers. The memory occupied by the returned array is not affected by
// calls to ap_free().
char** ap_get_args(ArgParser *parser);

// Attempts to parse and return the positional arguments as a freshly-allocated
// array of integers. Exits with an error message on failure. The memory
// occupied by the returned array is not affected by calls to ap_free().
int* ap_get_args_as_ints(ArgParser *parser);

// Attempts to parse and return the positional arguments as a freshly-allocated
// array of doubles. Exits with an error message on failure. The memory
// occupied by the returned array is not affected by calls to ap_free().
double* ap_get_args_as_floats(ArgParser *parser);


// -------------------------------------------------------------------------
// Commands.
// -------------------------------------------------------------------------

// Register a command with its associated callback and helptext. The callback
// should accept an ArgParser instance as its sole parameter and return void.
ArgParser* ap_add_cmd(
    ArgParser *parser, char *name, char *help, void (*cb)(ArgParser *parser)
);

// Returns true if the parser has found a command.
bool ap_has_cmd(ArgParser *parser);

// Returns the command name, if the parser has found a command.
char* ap_get_cmd_name(ArgParser *parser);

// Returns the command's parser instance, if the parser has found a command.
ArgParser* ap_get_cmd_parser(ArgParser *parser);

// Returns a command parser's parent parser.
ArgParser* ap_get_parent(ArgParser *parser);


// -------------------------------------------------------------------------
// Utilities.
// -------------------------------------------------------------------------

// Print a parser instance to stdout.
void ap_print(ArgParser *parser);
