#!/usr/bin/env vpython3
# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Wrapper for running Telemetry benchmarks on Fuchsia devices."""

from __future__ import print_function

import os
import sys

import fuchsia_util
from gpu_tests import path_util


def main():
  telemetry_script = [
      os.path.join(path_util.GetChromiumSrcDir(), 'tools', 'perf',
                   'run_benchmark')
  ]
  return fuchsia_util.RunTestOnFuchsiaDevice(telemetry_script)


if __name__ == '__main__':
  sys.exit(main())
