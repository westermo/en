// -------------------------------------------------------------------------
// Clio: a minimalist argument-parsing library designed for building elegant
// command line interfaces.
//
// Author: Darren Mulholland <darren@mulholland.xyz>
// License: Public Domain
// -------------------------------------------------------------------------

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>


// -------------------------------------------------------------------------
// Local Declarations
// -------------------------------------------------------------------------


typedef struct ArgParser ArgParser;
typedef void (*CmdCB)(ArgParser *parser);


// -------------------------------------------------------------------------
// Utility Functions
// -------------------------------------------------------------------------


// Print a message to stderr and exit with a non-zero error code.
static void err(char *msg) {
    fprintf(stderr, "Error: %s.\n", msg);
    exit(1);
}


// Print to an automatically-allocated string. Returns NULL if an encoding
// error occurs or if sufficient memory cannot be allocated.
char* str(char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    if (len < 1) {
        return NULL;
    }
    va_end(args);

    char *string = malloc(len + 1);
    if (string == NULL) {
        return NULL;
    }

    va_start(args, fmt);
    vsnprintf(string, len + 1, fmt, args);
    va_end(args);

    return string;
}


// Duplicate a string, automatically allocating memory for the copy.
static char* str_dup(char *arg) {
    size_t len = strlen(arg) + 1;
    char *copy = malloc(len);
    return copy ? memcpy(copy, arg, len) : NULL;
}


// Attempt to parse a string as an integer value, exiting on failure.
static int try_str_to_int(char *arg) {
    char *endptr;
    errno = 0;
    long result = strtol(arg, &endptr, 0);

    if (errno == ERANGE || result > INT_MAX || result < INT_MIN) {
        err(str("'%s' is out of range", arg));
    }

    if (*endptr != '\0') {
        err(str("cannot parse '%s' as an integer", arg));
    }

    return (int) result;
}


// Attempt to parse a string as a double value, exiting on failure.
static double try_str_to_double(char *arg) {
    char *endptr;
    errno = 0;
    double result = strtod(arg, &endptr);

    if (errno == ERANGE) {
        err(str("'%s' is out of range", arg));
    }

    if (*endptr != '\0') {
        err(str("cannot parse '%s' as a float", arg));
    }

    return result;
}


// -------------------------------------------------------------------------
// Map
// -------------------------------------------------------------------------


// Callback type for freeing a value stored in a Map instance.
typedef void (*MapFreeCB)(void *value);


// A MapEntry instance stores a map entry as a key-value pair.
typedef struct MapEntry {
    char *key;
    void *value;
} MapEntry;


// A Map instance maps string keys to pointer values.
typedef struct Map {
    int len;
    int cap;
    MapEntry *entries;
    MapFreeCB free_cb;
} Map;


// Free the memory occupied by a Map instance and all its values.
static void map_free(Map *map) {

    // Maintain a list of freed values so we don't attempt to free the same
    // value twice.
    void **freed_values = malloc(sizeof(void*) * map->len);
    int len_freed_values = 0;

    // Loop over all the entries in the map.
    for (int i = 0; i < map->len; i++) {
        char *key = map->entries[i].key;
        void *value = map->entries[i].value;

        // Free the memory occupied by the entry's key.
        free(key);

        // Has this entry's value been freed already?
        bool freed = false;
        for (int j = 0; j < len_freed_values; j++) {
            if (freed_values[j] == value) {
                freed = true;
                break;
            }
        }

        // Free the memory occupied by the entry's value.
        if (!freed) {
            freed_values[len_freed_values++] = value;
            if (map->free_cb != NULL) {
                map->free_cb(value);
            }
        }
    }

    free(freed_values);
    free(map->entries);
    free(map);
}


// Initialize a new Map instance.
static Map* map_new(MapFreeCB callback) {
    Map *map = malloc(sizeof(Map));
    map->len = 0;
    map->cap = 10;
    map->entries = malloc(sizeof(MapEntry) * map->cap);
    map->free_cb = callback;
    return map;
}


// Add a key-value pair to a map.
static void map_add(Map *map, char *key, void *value) {

    // Do we need to increase the map's capacity?
    if (map->len == map->cap) {
        map->cap *= 2;
        map->entries = realloc(map->entries, sizeof(MapEntry) * map->cap);
    }

    // Make a copy of the key.
    char *copiedkey = str_dup(key);

    // Add a MapEntry instance to the map's entry list.
    map->entries[map->len++] = (MapEntry){.key = copiedkey, .value = value};
}


// Split the specified keystring into multiple keys and add a separate
// key-value pair to the map for each key.
static void map_add_splitkey(Map *map, char *keystr, void *value) {
    char *key;
    char *saveptr;
    char *keystr_cpy = str_dup(keystr);

    key = strtok_r(keystr_cpy, " ", &saveptr);
    while (key != NULL) {
        map_add(map, key, value);
        key = strtok_r(NULL, " ", &saveptr);
    }

    free(keystr_cpy);
}


// Test if a Map instance contains the specified key.
static bool map_contains(Map *map, char *key) {
    for (int i = 0; i < map->len; i++) {
        if (strcmp(key, map->entries[i].key) == 0) {
            return true;
        }
    }
    return false;
}


// Retrieve a value from a Map instance. Returns NULL if the key is not found.
static void* map_get(Map *map, char *key) {
    for (int i = 0; i < map->len; i++) {
        if (strcmp(key, map->entries[i].key) == 0) {
            return map->entries[i].value;
        }
    }
    return NULL;
}


// Returns the key at the specified index.
static char* map_key_at_index(Map *map, int i) {
    return map->entries[i].key;
}


// Returns the value at the specified index.
static void* map_value_at_index(Map *map, int i) {
    return map->entries[i].value;
}


// -------------------------------------------------------------------------
// Options
// -------------------------------------------------------------------------


// We use 'flag' as a synonym for boolean options, i.e. options that are either
// present (true) or absent (false). All other option types require an argument.
typedef enum OptionType {
    FLAG,
    STRING,
    INTEGER,
    FLOAT,
} OptionType;


// Union combining all four valid types of option value.
typedef union OptionValue {
    bool bool_val;
    char *str_val;
    int int_val;
    double float_val;
} OptionValue;


// An Option instance represents an option registered on a parser.
typedef struct Option {
    OptionType type;
    bool found;
    bool greedy;
    int len;
    int cap;
    OptionValue *values;
} Option;


// Free the memory occupied by an Option instance.
static void option_free(Option *opt) {
    free(opt->values);
    free(opt);
}


// Callback for freeing an Option instance stored in a Map.
static void option_free_cb(void *opt) {
    option_free((Option*)opt);
}


// Clear an Option instance's internal list of values.
static void option_clear(Option *opt) {
    opt->len = 0;
}


// Append a value to an Option instance's internal list of values.
static void option_append(Option *opt, OptionValue value) {
    if (opt->len == opt->cap) {
        opt->cap *= 2;
        opt->values = realloc(opt->values, sizeof(OptionValue) * opt->cap);
    }
    opt->values[opt->len++] = value;
}


// Append a value to a boolean option's internal list.
static void option_set_flag(Option *opt, bool value) {
    option_append(opt, (OptionValue){.bool_val = value});
}


// Append a value to a string option's internal list.
static void option_set_str(Option *opt, char *value) {
    option_append(opt, (OptionValue){.str_val = value});
}


// Append a value to an integer option's internal list.
static void option_set_int(Option *opt, int value) {
    option_append(opt, (OptionValue){.int_val = value});
}


// Append a value to a floating-point option's internal list.
static void option_set_float(Option *opt, double value) {
    option_append(opt, (OptionValue){.float_val = value});
}


// Try setting an option by parsing the value of a string argument. Exits with
// an error message on failure.
static void option_try_set(Option *opt, char *arg) {
    if (opt->type == STRING) {
        option_set_str(opt, arg);
    }
    else if (opt->type == INTEGER) {
        option_set_int(opt, try_str_to_int(arg));
    }
    else if (opt->type == FLOAT) {
        option_set_float(opt, try_str_to_double(arg));
    }
}


// Initialize a new Option instance.
static Option* option_new() {
    Option *option = malloc(sizeof(Option));
    option->found = false;
    option->greedy = false;
    option->len = 0;
    option->cap = 1;
    option->values = malloc(sizeof(OptionValue) * option->cap);
    return option;
}


// Initialize a boolean option.
static Option* option_new_flag() {
    Option *opt = option_new();
    opt->type = FLAG;
    option_set_flag(opt, false);
    return opt;
}


// Initialize a string option with a default value.
static Option* option_new_str(char *value) {
    Option *opt = option_new();
    opt->type = STRING;
    option_set_str(opt, value);
    return opt;
}


// Initialize an integer option with a default value.
static Option* option_new_int(int value) {
    Option *opt = option_new();
    opt->type = INTEGER;
    option_set_int(opt, value);
    return opt;
}


// Initialize a floating-point option with a default value.
static Option* option_new_float(double value) {
    Option *opt = option_new();
    opt->type = FLOAT;
    option_set_float(opt, value);
    return opt;
}


// Initialize a boolean list option.
static Option* option_new_flag_list() {
    Option *opt = option_new();
    opt->type = FLAG;
    return opt;
}


// Initialize a string list option.
static Option* option_new_str_list(bool greedy) {
    Option *opt = option_new();
    opt->type = STRING;
    opt->greedy = greedy;
    return opt;
}


// Initialize an integer list option.
static Option* option_new_int_list(bool greedy) {
    Option *opt = option_new();
    opt->type = INTEGER;
    opt->greedy = greedy;
    return opt;
}


// Initialize a floating-point list option.
static Option* option_new_float_list(bool greedy) {
    Option *opt = option_new();
    opt->type = FLOAT;
    opt->greedy = greedy;
    return opt;
}


// Returns the value of a boolean option.
static bool option_get_flag(Option *opt) {
    return opt->values[opt->len - 1].bool_val;
}


// Returns the value of a string option.
static char* option_get_str(Option *opt) {
    return opt->values[opt->len - 1].str_val;
}


// Returns the value of an integer option.
static int option_get_int(Option *opt) {
    return opt->values[opt->len - 1].int_val;
}


// Returns the value of a floating-point option.
static double option_get_float(Option *opt) {
    return opt->values[opt->len - 1].float_val;
}


// Returns a list-option's values as a freshly-allocated array of bools.
static bool* option_get_flag_list(Option *opt) {
    if (opt->len == 0) {
        return NULL;
    }
    bool *list = malloc(sizeof(bool) * opt->len);
    for (int i = 0; i < opt->len; i++) {
        list[i] = opt->values[i].bool_val;
    }
    return list;
}


// Returns a list-option's values as a freshly-allocated array of strings.
static char** option_get_str_list(Option *opt) {
    if (opt->len == 0) {
        return NULL;
    }
    char **list = malloc(sizeof(char*) * opt->len);
    for (int i = 0; i < opt->len; i++) {
        list[i] = opt->values[i].str_val;
    }
    return list;
}


// Returns a list-option's values as a freshly-allocated array of integers.
static int* option_get_int_list(Option *opt) {
    if (opt->len == 0) {
        return NULL;
    }
    int *list = malloc(sizeof(int) * opt->len);
    for (int i = 0; i < opt->len; i++) {
        list[i] = opt->values[i].int_val;
    }
    return list;
}


// Returns a list-option's values as a freshly-allocated array of doubles.
static double* option_get_float_list(Option *opt) {
    if (opt->len == 0) {
        return NULL;
    }
    double *list = malloc(sizeof(double) * opt->len);
    for (int i = 0; i < opt->len; i++) {
        list[i] = opt->values[i].float_val;
    }
    return list;
}


// Returns a freshly-allocated string representing an Option instance.
static char* option_str(Option *opt) {
    char *tmpstr = str_dup("[");

    for (int i = 0; i < opt->len; i++) {
        char *valstr = NULL;

        if (opt->type == FLAG) {
            valstr = str_dup(opt->values[i].bool_val ? "true" : "false");
        } else if (opt->type == STRING) {
            valstr = str_dup(opt->values[i].str_val);
        } else if (opt->type == INTEGER) {
            valstr = str("%i", opt->values[i].int_val);
        } else if (opt->type == FLOAT) {
            valstr = str("%f", opt->values[i].float_val);
        }

        char *tmpstr_old = tmpstr;
        char *format = (i == 0) ? "%s%s" : "%s, %s";
        tmpstr = str(format, tmpstr_old, valstr);

        free(tmpstr_old);
        free(valstr);
    }

    char *outstr = str("%s%s", tmpstr, "]");
    free(tmpstr);

    return outstr;
}


// -------------------------------------------------------------------------
// ArgStream
// -------------------------------------------------------------------------


// An ArgStream instance is a wrapper for an array of string pointers,
// allowing it to be accessed as a stream.
typedef struct ArgStream {
    int len;
    int index;
    char **args;
} ArgStream;


// Free the memory associated with an ArgStream instance.
static void argstream_free(ArgStream *stream) {
    free(stream);
}


// Initialize a new ArgStream instance.
static ArgStream* argstream_new(int len, char **args) {
    ArgStream *stream = malloc(sizeof(ArgStream));
    stream->len = len;
    stream->index = 0;
    stream->args = args;
    return stream;
}


// Returns the next argument from the stream.
static char* argstream_next(ArgStream *stream) {
    return stream->args[stream->index++];
}


// Returns the next argument from the stream without consuming it.
static char* argstream_peek(ArgStream *stream) {
    return stream->args[stream->index];
}


// Returns true if the stream contains at least one more element.
static bool argstream_has_next(ArgStream *stream) {
    return stream->index < stream->len;
}


// Returns true if the stream contains at least one more element and that
// element has the form of an option value.
static bool argstream_has_next_value(ArgStream *stream) {
    if (argstream_has_next(stream)) {
        char *next = argstream_peek(stream);
        if (next[0] == '-') {
            if (strlen(next) == 1 || isdigit(next[1])) {
                return true;
            } else {
                return false;
            }
        } else {
            return true;
        }
    }
    return false;
}


// -------------------------------------------------------------------------
// ArgList
// -------------------------------------------------------------------------


// Container for storing positional arguments parsed from the input stream.
typedef struct ArgList {
    int len;
    int cap;
    char **args;
} ArgList;


// Free the memory associated with an ArgList instance.
static void arglist_free(ArgList *list) {
    free(list->args);
    free(list);
}


// Initialize a new ArgList instance.
static ArgList* arglist_new() {
    ArgList *list = malloc(sizeof(ArgList));
    list->len = 0;
    list->cap = 10;
    list->args = malloc(sizeof(char*) * list->cap);
    return list;
}


// Append an argument to the list.
static void arglist_append(ArgList *list, char *arg) {
    if (list->len == list->cap) {
        list->cap *= 2;
        list->args = realloc(list->args, sizeof(char*) * list->cap);
    }
    list->args[list->len] = arg;
    list->len++;
}


// Print the ArgList instance to stdout.
static void arglist_print(ArgList *list) {
    puts("Arguments:");
    if (list->len > 0) {
        for (int i = 0; i < list->len; i++) {
            printf("  %s\n", list->args[i]);
        }
    } else {
        puts("  [none]");
    }
}


// -------------------------------------------------------------------------
// ArgParser
// -------------------------------------------------------------------------


// An ArgParser instance is responsible for storing registered options and
// commands. Note that every registered command recursively receives an
// ArgParser instance of its own. In theory commands can be stacked to any
// depth, although in practice even two levels is confusing for users.
typedef struct ArgParser {
    char *helptext;
    char *version;
    Map *options;
    Map *commands;
    Map *callbacks;
    ArgList *arguments;
    char *cmd_name;
    ArgParser *cmd_parser;
    ArgParser *parent;
} ArgParser;


// Free the memory associated with an ArgParser instance.
static void argparser_free(ArgParser *parser) {
    map_free(parser->options);
    map_free(parser->commands);
    map_free(parser->callbacks);
    arglist_free(parser->arguments);
    free(parser);
}


// Callback for freeing an ArgParser instance stored in a Map.
static void argparser_free_cb(void *parser) {
    argparser_free((ArgParser*)parser);
}


// Initialize a new ArgParser instance. Supplying help text activates the
// automatic --help flag, supplying a version string activates the automatic
// --version flag. NULL can be passed for either parameter.
static ArgParser* argparser_new(char *helptext, char *version) {
    ArgParser *parser = malloc(sizeof(ArgParser));
    parser->helptext = helptext;
    parser->version = version;
    parser->options = map_new(option_free_cb);
    parser->commands = map_new(argparser_free_cb);
    parser->callbacks = map_new(NULL);
    parser->arguments = arglist_new();
    parser->cmd_name = NULL;
    parser->cmd_parser = NULL;
    parser->parent = NULL;
    return parser;
}


// -------------------------------------------------------------------------
// ArgParser: register options.
// -------------------------------------------------------------------------


// Register a boolean option with a default value of false.
static void argparser_add_flag(ArgParser *parser, char *name) {
    Option *opt = option_new_flag();
    map_add_splitkey(parser->options, name, opt);
}


// Register a string option with a default value.
static void argparser_add_str(ArgParser *parser, char *name, char* value) {
    Option *opt = option_new_str(value);
    map_add_splitkey(parser->options, name, opt);
}


// Register an integer option with a default value.
static void argparser_add_int(ArgParser *parser, char *name, int value) {
    Option *opt = option_new_int(value);
    map_add_splitkey(parser->options, name, opt);
}


// Register a float option with a default value.
static void argparser_add_float(ArgParser *parser, char *name, double value) {
    Option *opt = option_new_float(value);
    map_add_splitkey(parser->options, name, opt);
}


// Register a boolean list option.
static void argparser_add_flag_list(ArgParser *parser, char *name) {
    Option *opt = option_new_flag_list();
    map_add_splitkey(parser->options, name, opt);
}


// Register a string list option.
static void argparser_add_str_list(ArgParser *parser, char *name, bool greedy) {
    Option *opt = option_new_str_list(greedy);
    map_add_splitkey(parser->options, name, opt);
}


// Register an integer list option.
static void argparser_add_int_list(ArgParser *parser, char *name, bool greedy) {
    Option *opt = option_new_int_list(greedy);
    map_add_splitkey(parser->options, name, opt);
}


// Register a floating-point list option.
static void argparser_add_float_list(ArgParser *parser, char *name, bool greedy) {
    Option *opt = option_new_float_list(greedy);
    map_add_splitkey(parser->options, name, opt);
}


// -------------------------------------------------------------------------
// ArgParser: retrieve option values.
// -------------------------------------------------------------------------


// Retrieve a registered Option instance.
static Option* argparser_get_opt(ArgParser *parser, char *name) {
    Option *opt = map_get(parser->options, name);
    if (opt == NULL) {
        fprintf(stderr, "Abort: '%s' is not a registered option.\n", name);
        exit(1);
    }
    return opt;
}


// Returns true if the specified option was found while parsing.
static bool argparser_found(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return opt->found;
}


// Returns the value of the specified boolean option.
static bool argparser_get_flag(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_flag(opt);
}


// Returns the value of the specified string option.
static char* argparser_get_str(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_str(opt);
}


// Returns the value of the specified integer option.
static int argparser_get_int(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_int(opt);
}


// Returns the value of the specified floating-point option.
static double argparser_get_float(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_float(opt);
}


// Returns the length of the specified option's internal list of values.
static int argparser_len_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return opt->len;
}


// Returns an option's values as a freshly-allocated array of booleans. The
// array's memory is not affected by calls to argparser_free().
static bool* argparser_get_flag_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_flag_list(opt);
}


// Returns an option's values as a freshly-allocated array of string pointers.
// The array's memory is not affected by calls to argparser_free().
static char** argparser_get_str_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_str_list(opt);
}


// Returns an option's values as a freshly-allocated array of integers. The
// array's memory is not affected by calls to argparser_free().
static int* argparser_get_int_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_int_list(opt);
}


// Returns an option's values as a freshly-allocated array of doubles. The
// array's memory is not affected by calls to argparser_free().
static double* argparser_get_float_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_get_float_list(opt);
}


// -------------------------------------------------------------------------
// ArgParser: set option values.
// -------------------------------------------------------------------------


// Clear the specified option's internal list of values.
static void argparser_clear_list(ArgParser *parser, char *name) {
    Option *opt = argparser_get_opt(parser, name);
    return option_clear(opt);
}


// Append a value to a boolean option's internal list.
static void argparser_set_flag(ArgParser *parser, char *name, bool value) {
    Option *opt = argparser_get_opt(parser, name);
    option_set_flag(opt, value);
}


// Append a value to a string option's internal list.
static void argparser_set_str(ArgParser *parser, char *name, char *value) {
    Option *opt = argparser_get_opt(parser, name);
    option_set_str(opt, value);
}


// Append a value to an integer option's internal list.
static void argparser_set_int(ArgParser *parser, char *name, int value) {
    Option *opt = argparser_get_opt(parser, name);
    option_set_int(opt, value);
}


// Append a value to a floating-point option's internal list.
static void argparser_set_float(ArgParser *parser, char *name, double value) {
    Option *opt = argparser_get_opt(parser, name);
    option_set_float(opt, value);
}


// -------------------------------------------------------------------------
// ArgParser: positional arguments.
// -------------------------------------------------------------------------


// Returns true if the parser has found one or more positional arguments.
static bool argparser_has_args(ArgParser *parser) {
    return parser->arguments->len > 0;
}


// Returns the number of positional arguments.
static int argparser_len_args(ArgParser *parser) {
    return parser->arguments->len;
}


// Returns the positional argument at the specified index.
static char* argparser_get_arg(ArgParser *parser, int index) {
    return parser->arguments->args[index];
}


// Returns the positional arguments as a freshly-allocated array of string
// pointers. The memory occupied by the returned array is not affected by
// calls to argparser_free().
static char** argparser_get_args(ArgParser *parser) {
    char **args = malloc(sizeof(char*) * parser->arguments->len);
    memcpy(args, parser->arguments->args, sizeof(char*) * parser->arguments->len);
    return args;
}


// Attempts to parse and return the positional arguments as a freshly-allocated
// array of integers. Exits with an error message on failure. The memory
// occupied by the returned array is not affected by calls to argparser_free().
static int* argparser_get_args_as_ints(ArgParser *parser) {
    int *args = malloc(sizeof(int) * parser->arguments->len);
    for (int i = 0; i < parser->arguments->len; i++) {
        *(args + i) = try_str_to_int(parser->arguments->args[i]);
    }
    return args;
}


// Attempts to parse and return the positional arguments as a freshly-allocated
// array of doubles. Exits with an error message on failure. The memory
// occupied by the returned array is not affected by calls to argparser_free().
static double* argparser_get_args_as_floats(ArgParser *parser) {
    double *args = malloc(sizeof(double) * parser->arguments->len);
    for (int i = 0; i < parser->arguments->len; i++) {
        *(args + i) = try_str_to_double(parser->arguments->args[i]);
    }
    return args;
}


// -------------------------------------------------------------------------
// ArgParser: commands.
// -------------------------------------------------------------------------


// Register a command and its associated callback.
static ArgParser* argparser_add_cmd(
    ArgParser *parser, char *name, char *helptext, CmdCB callback
) {
    ArgParser *cmd_parser = argparser_new(helptext, NULL);
    cmd_parser->parent = parser;
    map_add_splitkey(parser->commands, name, cmd_parser);
    map_add_splitkey(parser->callbacks, name, callback);
    return cmd_parser;
}


// Returns true if the parser has found a command.
static bool argparser_has_cmd(ArgParser *parser) {
    return parser->cmd_name != NULL;
}


// Returns the command name, if the parser has found a command.
static char* argparser_get_cmd_name(ArgParser *parser) {
    return parser->cmd_name;
}


// Returns the command's parser instance, if the parser has found a command.
static ArgParser* argparser_get_cmd_parser(ArgParser *parser) {
    return parser->cmd_parser;
}


// Returns a command parser's parent parser.
static ArgParser* argparser_get_parent(ArgParser *parser) {
    return parser->parent;
}


// -------------------------------------------------------------------------
// ArgParser: parse arguments.
// -------------------------------------------------------------------------


// Parse an option of the form --name=value or -n=value.
static void argparser_parse_equals_option(
    ArgParser *parser, char *prefix, char *arg
) {
    char *name = str_dup(arg);
    *strchr(name, '=') = '\0';
    char *value = strchr(arg, '=') + 1;

    // Do we have the name of a registered option?
    Option *opt = map_get(parser->options, name);
    if (opt == NULL) {
        err(str("%s%s is not a recognised option", prefix, name));
    }
    opt->found = true;

    // Boolean flags can never contain an equals sign.
    if (opt->type == FLAG) {
        err(str("invalid format for boolean flag %s%s", prefix, name));
    }

    // Check that a value has been supplied.
    if (strlen(value) == 0) {
        err(str("missing argument for the %s%s option", prefix, name));
    }

    option_try_set(opt, value);

    // The map stores a copy of the name as its key so we can safely free
    // the memory used by our duplicated argument string.
    free(name);
}


// Parse a long-form option, i.e. an option beginning with a double dash.
static void argparser_parse_long_option(
    ArgParser *parser, char *arg, ArgStream *stream
) {
    // Do we have an option of the form --name=value?
    if (strstr(arg, "=") != NULL) {
        argparser_parse_equals_option(parser, "--", arg);
    }

    // Is the argument a registered option name?
    else if (map_contains(parser->options, arg)) {
        Option *opt = map_get(parser->options, arg);
        opt->found = true;

        // If the option is a flag, store the boolean true.
        if (opt->type == FLAG) {
            option_set_flag(opt, true);
        }

        // Not a flag so check for a following option value.
        else if (argstream_has_next_value(stream)) {

            // Try to parse the argument as a value of the appropriate type.
            option_try_set(opt, argstream_next(stream));

            // If the option is a greedy list, keep trying to parse values
            // until we run out of arguments.
            if (opt->greedy) {
                while (argstream_has_next_value(stream)) {
                    option_try_set(opt, argstream_next(stream));
                }
            }
        }

        // We're missing a required option value.
        else {
            err(str("missing argument for the --%s option", arg));
        }
    }

    // Is the argument the automatic --help flag?
    else if (strcmp(arg, "help") == 0 && parser->helptext != NULL) {
        puts(parser->helptext);
        exit(0);
    }

    // Is the argument the automatic --version flag?
    else if (strcmp(arg, "version") == 0 && parser->version != NULL) {
        puts(parser->version);
        exit(0);
    }

    // The argument is not a registered or automatic option name.
    // Print an error message and exit.
    else {
        err(str("--%s is not a recognised option", arg));
    }
}


// Parse a short-form option, i.e. an option beginning with a single dash.
static void argparser_parse_short_option(
    ArgParser *parser, char *arg, ArgStream *stream
) {
    // Do we have an option of the form -n=value?
    if (strstr(arg, "=") != NULL) {
        argparser_parse_equals_option(parser, "-", arg);
        return;
    }

    // We handle each character individually to support condensed options:
    //    -abc foo bar
    // is equivalent to:
    //    -a foo -b bar -c
    for (size_t i = 0; i < strlen(arg); i++) {

        // Do we have the name of a registered option?
        char key[] = {arg[i], 0};
        Option *opt = map_get(parser->options, key);
        if (opt == NULL) {
            err(str("-%s is not a recognised option", key));
        }
        opt->found = true;

        // If the option is a flag, store the boolean true.
        if (opt->type == FLAG) {
            option_set_flag(opt, true);
        }

        // Not a flag so check for a following option value.
        else if (argstream_has_next_value(stream)) {

            // Try to parse the argument as a value of the appropriate type.
            option_try_set(opt, argstream_next(stream));

            // If the option is a greedy list, keep trying to parse values
            // until we run out of arguments.
            if (opt->greedy) {
                while (argstream_has_next_value(stream)) {
                    option_try_set(opt, argstream_next(stream));
                }
            }
        }

        // We're missing a required option value.
        else {
            err(str("missing argument for the -%s option", key));
        }
    }
}


// Parse a stream of string arguments.
static void argparser_parse_stream(ArgParser *parser, ArgStream *stream) {

    // Switch to turn off option parsing if we encounter a double dash, '--'.
    // Everything following the '--' will be treated as a positional argument.
    bool parsing = true;

    // Loop while we have arguments to process.
    while (argstream_has_next(stream)) {

        // Fetch the next argument from the stream.
        char *arg = argstream_next(stream);

        // If parsing has been turned off, simply add the argument to the list
        // of positionals.
        if (!parsing) {
            arglist_append(parser->arguments, arg);
            continue;
        }

        // If we encounter a '--' argument, turn off option-parsing.
        if (strcmp(arg, "--") == 0) {
            parsing = false;
            continue;
        }

        // Is the argument a long-form option or flag?
        else if (strncmp(arg, "--", 2) == 0) {
            argparser_parse_long_option(parser, arg + 2, stream);
        }

        // Is the argument a short-form option or flag? If the argument
        // consists of a single dash or a dash followed by a digit, we treat
        // it as a positional argument.
        else if (arg[0] == '-') {
            if (strlen(arg) == 1 || isdigit(arg[1])) {
                arglist_append(parser->arguments, arg);
            } else {
                argparser_parse_short_option(parser, arg + 1, stream);
            }
        }

        // Is the argument a registered command?
        else if (map_contains(parser->commands, arg)) {
            ArgParser *cmd_parser = map_get(parser->commands, arg);
            CmdCB cmd_callback = map_get(parser->callbacks, arg);
            parser->cmd_name = arg;
            parser->cmd_parser = cmd_parser;
            argparser_parse_stream(cmd_parser, stream);
            if (cmd_callback != NULL) {
                cmd_callback(cmd_parser);
            }
        }

        // Is the argument the automatic 'help' command? The commands
        //     $ app cmd --help
        // and
        //     $ app help cmd
        // are functionally equivalent. Both will print the help text
        // associated with the command.
        else if (strcmp(arg, "help") == 0) {
            if (argstream_has_next(stream)) {
                char *name = argstream_next(stream);
                if (map_contains(parser->commands, name)) {
                    ArgParser *cmd_parser = map_get(parser->commands, name);
                    puts(cmd_parser->helptext);
                    exit(0);
                } else {
                    err(str("'%s' is not a recognised command", name));
                }
            } else {
                err(str("the help command requires an argument"));
            }
        }

        // Otherwise add the argument to our list of positional arguments.
        else {
            arglist_append(parser->arguments, arg);
        }
    }
}


// Parse an array of string arguments.
static void argparser_parse(ArgParser *parser, int len, char *args[]) {
    ArgStream *stream = argstream_new(len, args);
    argparser_parse_stream(parser, stream);
    argstream_free(stream);
}


// -------------------------------------------------------------------------
// ArgParser: utilities.
// -------------------------------------------------------------------------


// Print a parser instance to stdout.
static void argparser_print(ArgParser *parser) {
    puts("Options:");
    if (parser->options->len > 0) {
        for (int i = 0; i < parser->options->len; i++) {
            char *name = map_key_at_index(parser->options, i);
            Option *opt = map_value_at_index(parser->options, i);
            char *optstr = option_str(opt);
            printf("  %s: %s\n", name, optstr);
            free(optstr);
        }
    } else {
        puts("  [none]");
    }
    puts("");

    arglist_print(parser->arguments);

    puts("\nCommand:");
    if (argparser_has_cmd(parser)) {
        printf("  %s\n", argparser_get_cmd_name(parser));
    } else {
        puts("  [none]");
    }
}


// -------------------------------------------------------------------------
// Public Interface
// -------------------------------------------------------------------------


ArgParser* ap_new(char *helptext, char *version) {
    return argparser_new(helptext, version);
}


void ap_free(ArgParser *parser) {
    argparser_free(parser);
}


void ap_parse(ArgParser *parser, int argc, char *argv[]) {
    argparser_parse(parser, argc - 1, argv + 1);
}


void ap_add_flag(ArgParser *parser, char *name) {
    argparser_add_flag(parser, name);
}


void ap_add_str(ArgParser *parser, char *name, char* value) {
    argparser_add_str(parser, name, value);
}


void ap_add_int(ArgParser *parser, char *name, int value) {
    argparser_add_int(parser, name, value);
}


void ap_add_float(ArgParser *parser, char *name, double value) {
    argparser_add_float(parser, name, value);
}


void ap_add_flag_list(ArgParser *parser, char *name) {
    argparser_add_flag_list(parser, name);
}


void ap_add_str_list(ArgParser *parser, char *name, bool greedy) {
    argparser_add_str_list(parser, name, greedy);
}


void ap_add_int_list(ArgParser *parser, char *name, bool greedy) {
    argparser_add_int_list(parser, name, greedy);
}


void ap_add_float_list(ArgParser *parser, char *name, bool greedy) {
    argparser_add_float_list(parser, name, greedy);
}


ArgParser* ap_add_cmd(ArgParser *parser, char *name, char *help, CmdCB cb) {
    return argparser_add_cmd(parser, name, help, cb);
}


bool ap_found(ArgParser *parser, char *name) {
    return argparser_found(parser, name);
}


bool ap_get_flag(ArgParser *parser, char *name) {
    return argparser_get_flag(parser, name);
}


char* ap_get_str(ArgParser *parser, char *name) {
    return argparser_get_str(parser, name);
}


int ap_get_int(ArgParser *parser, char *name) {
    return argparser_get_int(parser, name);
}


double ap_get_float(ArgParser *parser, char *name) {
    return argparser_get_float(parser, name);
}


bool* ap_get_flag_list(ArgParser *parser, char *name) {
    return argparser_get_flag_list(parser, name);
}


char** ap_get_str_list(ArgParser *parser, char *name) {
    return argparser_get_str_list(parser, name);
}


int* ap_get_int_list(ArgParser *parser, char *name) {
    return argparser_get_int_list(parser, name);
}


double* ap_get_float_list(ArgParser *parser, char *name) {
    return argparser_get_float_list(parser, name);
}


int ap_len_list(ArgParser *parser, char *name) {
    return argparser_len_list(parser, name);
}


void ap_clear_list(ArgParser *parser, char *name) {
    argparser_clear_list(parser, name);
}


void ap_set_flag(ArgParser *parser, char *name, bool value) {
    argparser_set_flag(parser, name, value);
}


void ap_set_str(ArgParser *parser, char *name, char *value) {
    argparser_set_str(parser, name, value);
}


void ap_set_int(ArgParser *parser, char *name, int value) {
    argparser_set_int(parser, name, value);
}


void ap_set_float(ArgParser *parser, char *name, double value) {
    argparser_set_float(parser, name, value);
}


bool ap_has_args(ArgParser *parser) {
    return argparser_has_args(parser);
}


int ap_len_args(ArgParser *parser) {
    return argparser_len_args(parser);
}


char* ap_get_arg(ArgParser *parser, int index) {
    return argparser_get_arg(parser, index);
}


char** ap_get_args(ArgParser *parser) {
    return argparser_get_args(parser);
}


int* ap_get_args_as_ints(ArgParser *parser) {
    return argparser_get_args_as_ints(parser);
}


double* ap_get_args_as_floats(ArgParser *parser) {
    return argparser_get_args_as_floats(parser);
}


bool ap_has_cmd(ArgParser *parser) {
    return argparser_has_cmd(parser);
}


char* ap_get_cmd_name(ArgParser *parser) {
    return argparser_get_cmd_name(parser);
}


ArgParser* ap_get_cmd_parser(ArgParser *parser) {
    return argparser_get_cmd_parser(parser);
}


ArgParser* ap_get_parent(ArgParser *parser) {
    return argparser_get_parent(parser);
}


void ap_print(ArgParser *parser) {
    argparser_print(parser);
}
