// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

console.log('Start executing simple_module.js')

class TestOperation {
  async run(data) {
    console.log('Start executing \'test-operation\'');
    console.log(JSON.stringify(data, Object.keys(data).sort()));
    console.log('Finish executing \'test-operation\'');
  }
}

registerOperation("test-operation", TestOperation);

console.log('Finish executing simple_module.js')