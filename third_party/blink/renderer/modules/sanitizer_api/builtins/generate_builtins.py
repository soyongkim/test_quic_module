#!/usr/bin/env python
# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate constants derived from the Sanitizer API spec."""

import json
import optparse
import sys


def error(context, *infos):
    """Print a brief error message and return an error code."""
    messages = ["An error occurred when when " + context + ":"]
    messages.extend(infos)
    print("\n\t".join(map(str, messages)))
    return 1


def lstrip(string):
    """Call str.lstrip on each line of text."""
    return "\n".join(map(str.lstrip, string.split("\n")))


def prolog(out):
    """Print the beginning of the source file."""
    print(lstrip("""
        // Copyright 2021 The Chromium Authors. All rights reserved.
        // Use of this source code is governed by a BSD-style license that can be
        // found in the LICENSE file.

        // This file is automatically generated. Do not edit. Just generate.
        // $ ninja -C ... generate_sanitizer_builtins

        #include "third_party/blink/renderer/modules/sanitizer_api/builtins/sanitizer_builtins.h"

        namespace blink {
        """),
          file=out)


def epilog(out):
    """Print the end of the source file."""
    print(lstrip("""
        }  // namespace blink
        """), file=out)


def string_list(out, name, items):
    """Print a list of strings as a C++ array of const char*."""
    print(f"const char* const {name}[] = {{", file=out)
    for item in items:
        print(f"  \"{item}\",", file=out)
    print("  nullptr,", file=out)
    print("};", file=out)
    print("", file=out)
    pass


def main(argv):
    parser = optparse.OptionParser()
    parser.add_option("--out")
    parser.add_option("--baseline-elements")
    parser.add_option("--baseline-attributes")
    parser.add_option("--default-configuration")
    options, args = parser.parse_args(argv)
    for option, value in options.__dict__.items():
        if not value:
            parser.error(f"--{option} is required.")
    if args:
        parser.error("No positional arguments are supported.")

    try:
        baseline_elements = json.load(open(options.baseline_elements, "r"))
    except BaseException as err:
        return error("reading baseline elements", options.baseline_elements,
                     err)
    try:
        baseline_attributes = json.load(open(options.baseline_attributes, "r"))
    except BaseException as err:
        return error("reading baseline attributes",
                     options.baseline_attributes, err)
    try:
        default_configuration = json.load(
            open(options.default_configuration, "r"))
    except BaseException as err:
        return error("reading default configuration",
                     options.default_configuration, err)

    try:
        with open(options.out, "w") as output:
            prolog(output)
            string_list(output, "kBaselineElements", baseline_elements)
            string_list(output, "kBaselineAttributes", baseline_attributes)
            string_list(output, "kDefaultElements",
                        default_configuration["allowElements"])
            string_list(output, "kDefaultAttributes",
                        default_configuration["allowAttributes"].keys())
            epilog(output)
    except BaseException as err:
        return error("writing output file", options.out, err)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
