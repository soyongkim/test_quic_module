// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

var systemTokenEnabled = (location.search.indexOf("systemTokenEnabled") != -1);
var selectedTestSuite = location.hash.slice(1);
console.log('[SELECTED TEST SUITE] ' + selectedTestSuite +
            ', systemTokenEnable ' + systemTokenEnabled);

var assertEq = chrome.test.assertEq;
var assertTrue = chrome.test.assertTrue;
var assertFalse = chrome.test.assertFalse;
var fail = chrome.test.fail;
var succeed = chrome.test.succeed;
var callbackPass = chrome.test.callbackPass;
var callbackFail = chrome.test.callbackFail;
var checkDeepEq = chrome.test.checkDeepEq;

// Each value is the path to a file in this extension's folder that will be
// loaded and replaced by a Uint8Array in the setUp() function below.
var data = {
  // X.509 certificate in DER encoding issued by 'root.pem' which is set to be
  // trusted by the test setup.
  // Generated by create_test_certs.sh .
  trusted_l1_leaf_cert: 'l1_leaf.der',

  // X.509 intermediate CA certificate in DER encoding issued by 'root.pem'
  // which is set to be trusted by the test setup.
  // Generated by create_test_certs.sh .
  trusted_l1_interm_cert: 'l1_interm.der',

  // X.509 certificate in DER encoding issued by 'l1_interm'.
  // Generated by create_test_certs.sh .
  trusted_l2_leaf_cert: 'l2_leaf.der',

  // X.509 client certificate in DER encoding.
  // Algorithm in SPKI: rsaEncryption.
  // Generated by create_test_certs.sh .
  // Imported in the user token.
  client_1: 'client_1.der',

  // X.509 client certificate in DER encoding.
  // Algorithm in SPKI: rsaEncryption.
  // Generated by create_test_certs.sh .
  // Imported in the system token (if available).
  client_2: 'client_2.der',

  // X.509 client certificate in DER encoding.
  // Algorithm in SPKI: ECDSA Encryption.
  // Generated by create_test_certs.sh.
  // Imported in the user token.
  client_3: 'client_3.der',


  // The public key of client_1 as Subject Public Key Info in DER encoding.
  // Generated by create_test_certs.sh .
  client_1_spki: 'client_1_spki.der',

  // The public key of ec_cert as Subject Public Key Info in DER encoding.
  // Generated by create_test_certs.sh.
  ec_spki: 'ec_spki.der',

  // X.509 certificate in DER encoding for ec_spki.
  // Generated by create_test_certs.sh.
  ec_cert: 'ec_cert.der',

  // The distinguished name of the CA that issued client_1 in DER encoding.
  // Generated by create_test_certs.sh .
  client_1_issuer_dn: 'client_1_issuer_dn.der',

  // The string "hello world".
  // Generated by create_test_certs.sh .
  raw_data: 'data',

  // A signature of raw_data using RSASSA-PKCS1-v1_5 with client_1, but treating
  // raw_data as a raw digest and without adding the DigestInfo prefix.
  // Generated by create_test_certs.sh .
  signature_nohash_pkcs: 'signature_nohash_pkcs',

  // A signature of raw_data using RSASSA-PKCS1-v1_5 with client_1, using SHA-1
  // as the hash function.
  // Generated by create_test_certs.sh .
  signature_client1_sha1_pkcs: 'signature_client1_sha1_pkcs',

  // A signature of raw_data using RSASSA-PKCS1-v1_5 with client_2, using SHA-1
  // as the hash function.
  // Generated by create_test_certs.sh .
  signature_client2_sha1_pkcs: 'signature_client2_sha1_pkcs',
};

// Reads the binary file at |path| and passes it as a Uint8Array to |callback|.
function readFile(path, callback) {
  var oReq = new XMLHttpRequest();
  oReq.responseType = "arraybuffer";
  oReq.open("GET", path, true /* asynchronous */);
  oReq.onload = function() {
    var arrayBuffer = oReq.response;
    if (arrayBuffer) {
      callback(new Uint8Array(arrayBuffer));
    } else {
      callback(null);
    }
  };
  oReq.send(null);
}

// For each key in dictionary, replaces the path dictionary[key] by the content
// of the resource located at that path stored in a Uint8Array.
function readData(dictionary, callback) {
  var keys = Object.keys(dictionary);
  function recurse(index) {
    if (index >= keys.length) {
      callback();
      return;
    }
    var key = keys[index];
    var path = dictionary[key];
    readFile(path, function(array) {
      assertTrue(!!array);
      dictionary[key] = array;
      recurse(index + 1);
    });
  }

  recurse(0);
}

function setUp(callback) {
  readData(data, callback);
}

// Some array comparison. Note: not lexicographical!
function compareArrays(array1, array2) {
  if (array1.length < array2.length)
    return -1;
  if (array1.length > array2.length)
    return 1;
  for (var i = 0; i < array1.length; i++) {
    if (array1[i] < array2[i])
      return -1;
    if (array1[i] > array2[i])
      return 1;
  }
  return 0;
}

/**
 * @param {ArrayBufferView[]} certs
 * @return {ArrayBufferView[]} |certs| sorted in some order.
 */
function sortCerts(certs) {
  return certs.sort(compareArrays);
}

function assertCertsSelected(details, expectedCerts, callback) {
  chrome.platformKeys.selectClientCertificates(
      details, callbackPass(function(actualMatches) {
        assertEq(expectedCerts.length, actualMatches.length,
                 'Number of stored certs not as expected');
        if (expectedCerts.length == actualMatches.length) {
          var actualCerts = actualMatches.map(function(match) {
            return new Uint8Array(match.certificate);
          });
          actualCerts = sortCerts(actualCerts);
          expectedCerts = sortCerts(expectedCerts);
          for (var i = 0; i < expectedCerts.length; i++) {
            assertEq(expectedCerts[i], actualCerts[i],
                     'Certs at index ' + i + ' differ');
          }
        }
        if (callback)
          callback();
      }));
}

function checkRsaAlgorithmIsCopiedOnRead(key) {
  const algorithm = key.algorithm;
  const originalAlgorithm = {
    name: algorithm.name,
    modulusLength: algorithm.modulusLength,
    publicExponent: algorithm.publicExponent,
    hash: {name: algorithm.hash.name}
  };
  algorithm.hash.name = null;
  algorithm.hash = null;
  algorithm.name = null;
  algorithm.modulusLength = null;
  algorithm.publicExponent = null;
  assertEq(originalAlgorithm, key.algorithm);
}

function checkEcAlgorithmIsCopiedOnRead(key) {
  const algorithm = key.algorithm;
  const originalAlgorithm = {
    name: algorithm.name,
    namedCurve: algorithm.namedCurve,
  };
  algorithm.name = null;
  algorithm.namedCurve = null;
  assertEq(originalAlgorithm, key.algorithm);
}

function checkAlgorithmIsCopiedOnRead(key) {
  const algorithmName = key.algorithm.name;
  assertTrue(
      algorithmName === 'RSASSA-PKCS1-v1_5' || algorithmName === 'ECDSA');
  if (algorithmName === 'RSASSA-PKCS1-v1_5') {
    checkRsaAlgorithmIsCopiedOnRead(key);
  } else {
    checkEcAlgorithmIsCopiedOnRead(key);
  }
}

function checkPropertyIsReadOnly(object, key) {
  var original = object[key];
  try {
    object[key] = {};
    fail('Expected the property ' + key +
         ' to be read-only and an exception to be thrown');
  } catch (error) {
    assertEq(original, object[key]);
  }
}

function checkPrivateKeyFormat(privateKey) {
  assertEq('private', privateKey.type);
  assertEq(false, privateKey.extractable);
  checkPropertyIsReadOnly(privateKey, 'algorithm');
  checkAlgorithmIsCopiedOnRead(privateKey);
}

function checkPublicKeyFormat(publicKey) {
  assertEq('public', publicKey.type);
  assertEq(true, publicKey.extractable);
  checkPropertyIsReadOnly(publicKey, 'algorithm');
  checkAlgorithmIsCopiedOnRead(publicKey);
}

function testStaticMethods() {
  assertTrue(!!chrome.platformKeys, "No platformKeys namespace.");
  assertTrue(!!chrome.platformKeys.selectClientCertificates,
             "No selectClientCertificates function.");
  assertTrue(!!chrome.platformKeys.getKeyPair, "No getKeyPair method.");
  assertTrue(
      !!chrome.platformKeys.getKeyPairBySpki, "No getKeyPairBySpki method.");
  assertTrue(!!chrome.platformKeys.subtleCrypto, "No subtleCrypto getter.");
  assertTrue(!!chrome.platformKeys.subtleCrypto(), "No subtleCrypto object.");
  assertTrue(!!chrome.platformKeys.subtleCrypto().sign, "No sign method.");
  assertTrue(!!chrome.platformKeys.subtleCrypto().exportKey,
             "No exportKey method.");
  succeed();
}

var requestAll = {
  certificateTypes: [],
  certificateAuthorities: []
};

// Depends on |data|, thus it cannot be created immediately.
function requestCA1() {
  return {
    certificateTypes: [],
    certificateAuthorities: [data.client_1_issuer_dn.buffer]
  };
}

function testSelectAllCerts() {
  var expectedCerts = [data.client_1, data.client_3];
  if (systemTokenEnabled)
    expectedCerts.push(data.client_2);
  assertCertsSelected({interactive: false, request: requestAll}, expectedCerts);
}

function testSelectWithInputClientCerts() {
  var expectedCerts = [];
  if (systemTokenEnabled)
    expectedCerts.push(data.client_2);
  assertCertsSelected(
      {
        interactive: false,
        request: requestAll,
        clientCerts: [data.client_2.buffer]
      },
      expectedCerts);
}

function testSelectCA1Certs() {
  assertCertsSelected({interactive: false, request: requestCA1()},
                      [data.client_1]);
}

function testSelectAllReturnsNoCerts() {
  assertCertsSelected({interactive: false, request: requestAll},
                      [] /* no certs selected */);
}

function testSelectAllReturnsClient1() {
  assertCertsSelected({interactive: false, request: requestAll},
                      [data.client_1]);
}

function testInteractiveSelectNoCerts() {
  assertCertsSelected({interactive: true, request: requestAll},
                      [] /* no certs selected */);
}

function testInteractiveSelectClient1() {
  assertCertsSelected({interactive: true, request: requestAll},
                      [data.client_1]);
}

function testInteractiveSelectClient2() {
  var expectedCerts = [];
  if (systemTokenEnabled)
    expectedCerts.push(data.client_2);
  assertCertsSelected({interactive: true, request: requestAll}, expectedCerts);
}

function testInteractiveSelectClient3() {
  assertCertsSelected(
      {interactive: true, request: requestAll}, [data.client_3]);
}

function testMatchResultCA1() {
  chrome.platformKeys.selectClientCertificates(
      {interactive: false, request: requestCA1()},
      callbackPass(function(matches) {
        var expectedAlgorithm = {
          modulusLength: 2048,
          name: "RSASSA-PKCS1-v1_5",
          publicExponent: new Uint8Array([0x01, 0x00, 0x01])
        };
        var actualAlgorithm = matches[0].keyAlgorithm;
        assertEq(
            expectedAlgorithm, actualAlgorithm,
            'Member algorithm of Match does not equal the expected algorithm');
      }));
}

function testMatchResultECDSA() {
  var requestECDSA = {
    certificateTypes: ['ecdsaSign'],
    certificateAuthorities: []
  };
  assertCertsSelected(
      {interactive: false, request: requestECDSA}, [data.client_3]);
}

function testMatchResultRSA() {
  var requestRSA = {
    certificateTypes: ['rsaSign'],
    certificateAuthorities: []
  };
  chrome.platformKeys.selectClientCertificates(
      {interactive: false, request: requestRSA},
      callbackPass(function(matches) {
        var expectedAlgorithm = {
          modulusLength: 2048,
          name: "RSASSA-PKCS1-v1_5",
          publicExponent: new Uint8Array([0x01, 0x00, 0x01])
        };
        var actualAlgorithm = matches[0].keyAlgorithm;
        assertEq(
            expectedAlgorithm, actualAlgorithm,
            'Member algorithm of Match does not equal the expected algorithm');
      }));
}

function verifyMissingAlgorithmError(getKeyFunction, buffer, name) {
  var keyParams = {
    // This is missing the algorithm name.
    hash: {name: 'SHA-1'}
  };
  try {
    getKeyFunction(buffer, keyParams, function(error) {
      fail(`${name} call was expected to fail.`);
    });
    fail(`${name} did not throw error`);
  } catch (e) {
    assertEq('Algorithm: name: Missing or not a String', e.message);
    succeed();
  }
}

function testGetKeyPairMissingAlgorithmName() {
  verifyMissingAlgorithmError(
      chrome.platformKeys.getKeyPair, data.client_1.buffer, 'getKeyPair');
}

function testGetKeyPairBySpkiMissingAlgorithmName() {
  verifyMissingAlgorithmError(
      chrome.platformKeys.getKeyPairBySpki, data.client_1_spki.buffer,
      'getKeyPairBySpki');
}

function testGetKeyPairRejectsRSAPSS() {
  var keyParams = {
    name: 'RSA-PSS',
    hash: {name: 'SHA-1'}
  };
  chrome.platformKeys.getKeyPair(
      data.client_1.buffer, keyParams,
      callbackFail('Algorithm not supported.'));
  chrome.platformKeys.getKeyPairBySpki(
      data.client_1_spki.buffer, keyParams,
      callbackFail('Algorithm not supported.'));
}

function verifyRsaKeyPairValidity(publicKey, privateKey) {
  var expectedAlgorithm = {
    modulusLength: 2048,
    name: 'RSASSA-PKCS1-v1_5',
    publicExponent: new Uint8Array([0x01, 0x00, 0x01]),
    hash: {name: 'SHA-1'}
  };
  assertEq(expectedAlgorithm, publicKey.algorithm);
  assertEq(expectedAlgorithm, privateKey.algorithm);

  checkPublicKeyFormat(publicKey);
  checkPrivateKeyFormat(privateKey);

  chrome.platformKeys.subtleCrypto()
      .exportKey('spki', publicKey)
      .then(
          callbackPass(function(actualPublicKeySpki) {
            assertTrue(
                compareArrays(
                    data.client_1_spki, new Uint8Array(actualPublicKeySpki)) ==
                    0,
                'Match did not contain correct public key');
          }),
          function(error) {
            fail('Export failed: ' + error);
          });
}

const EC_KEY_PARAMS = {
  name: 'ECDSA',
  namedCurve: 'P-256'
};

async function verifyEcKeyPairValidity(publicKey, privateKey) {
  assertEq(EC_KEY_PARAMS, publicKey.algorithm);
  assertEq(EC_KEY_PARAMS, privateKey.algorithm);

  checkPublicKeyFormat(publicKey);
  checkPrivateKeyFormat(privateKey);

  let actualPublicKeySpki;
  try {
    actualPublicKeySpki =
        await chrome.platformKeys.subtleCrypto().exportKey('spki', publicKey);
  } catch (error) {
    fail('Export failed: ' + error);
  }
  const actualPublicKeySpkiArray = new Uint8Array(actualPublicKeySpki);
  assertEq(
      data.ec_spki, actualPublicKeySpkiArray,
      'Match did not contain correct public key');
  succeed();
}

function testGetRsaKeyPairRejectEcdsa() {
  const rsa_key = data.client_1;

  chrome.platformKeys.getKeyPair(
      rsa_key.buffer, EC_KEY_PARAMS,
      callbackFail(
          'The requested Algorithm is not permitted by the certificate.'));
}

function testGetEcKeyPairRejectRsa() {
  const RSA_KEY_PARAMS = {name: 'RSASSA-PKCS1-V1_5', hash: {name: 'SHA-1'}};

  chrome.platformKeys.getKeyPair(
      data.ec_cert.buffer, RSA_KEY_PARAMS,
      callbackFail(
          'The requested Algorithm is not permitted by the certificate.'));
}

function testGetKeyPairBySpkiRsaKeyRejectsEcdsa() {
  const rsa_key = data.client_1_spki;

  chrome.platformKeys.getKeyPairBySpki(
      rsa_key.buffer, EC_KEY_PARAMS,
      callbackFail(
          'The requested Algorithm is not permitted by the certificate.'));
}

function testGetKeyPairBySpkiEcKeyRejectsRsa() {
  const RSA_KEY_PARAMS = {name: 'RSASSA-PKCS1-V1_5', hash: {name: 'SHA-1'}};

  chrome.platformKeys.getKeyPairBySpki(
      data.ec_spki.buffer, RSA_KEY_PARAMS,
      callbackFail(
          'The requested Algorithm is not permitted by the certificate.'));
}

function testGetRsaKeyPair() {
  var keyParams = {
    // Algorithm names are case-insensitive.
    name: 'RSASSA-Pkcs1-V1_5',
    hash: {name: 'sha-1'}
  };
  chrome.platformKeys.getKeyPair(
      data.client_1.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifyRsaKeyPairValidity(publicKey, privateKey);
      }));
  chrome.platformKeys.getKeyPairBySpki(
      data.client_1_spki.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifyRsaKeyPairValidity(publicKey, privateKey);
      }));
}

function verifySignWithNoHash(privateKey, signParams) {
  chrome.platformKeys.subtleCrypto()
      .sign(signParams, privateKey, data.raw_data)
      .then(callbackPass(function(signature) {
        var actualSignature = new Uint8Array(signature);
        assertTrue(
            compareArrays(data.signature_nohash_pkcs, actualSignature) == 0,
            'Incorrect signature');
      }));
}

function testGetEcKeyPair() {
  chrome.platformKeys.getKeyPair(
      data.ec_cert.buffer, EC_KEY_PARAMS, function(publicKey, privateKey) {
        verifyEcKeyPairValidity(publicKey, privateKey);
      });

  chrome.platformKeys.getKeyPairBySpki(
      data.ec_spki.buffer, EC_KEY_PARAMS, function(publicKey, privateKey) {
        verifyEcKeyPairValidity(publicKey, privateKey);
      });
}

function testSignNoHash() {
  var keyParams = {
    // Algorithm names are case-insensitive.
    name: 'RSASSA-PKCS1-V1_5',
    hash: {name: 'NONE'}
  };
  var signParams = {
    name: 'RSASSA-PKCS1-v1_5'
  };
  chrome.platformKeys.getKeyPair(
      data.client_1.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifySignWithNoHash(privateKey, signParams);
      }));
  chrome.platformKeys.getKeyPairBySpki(
      data.client_1_spki.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifySignWithNoHash(privateKey, signParams);
      }));
}

function verifySignWithSha1(privateKey, signParams, client_signature) {
  chrome.platformKeys.subtleCrypto()
      .sign(signParams, privateKey, data.raw_data)
      .then(callbackPass(function(signature) {
        var actualSignature = new Uint8Array(signature);
        assertTrue(
            compareArrays(client_signature, actualSignature) == 0,
            'Incorrect signature');
      }));
}

function testSignSha1Client1() {
  var keyParams = {
    name: 'RSASSA-PKCS1-v1_5',
    // Algorithm names are case-insensitive.
    hash: {name: 'Sha-1'}
  };
  var signParams = {
    // Algorithm names are case-insensitive.
    name: 'RSASSA-Pkcs1-v1_5'
  };
  chrome.platformKeys.getKeyPair(
      data.client_1.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifySignWithSha1(
            privateKey, signParams, data.signature_client1_sha1_pkcs);
      }));
  chrome.platformKeys.getKeyPairBySpki(
      data.client_1_spki.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifySignWithSha1(
            privateKey, signParams, data.signature_client1_sha1_pkcs);
      }));
}

function testSignSha1Client2() {
  var keyParams = {
    name: 'RSASSA-PKCS1-v1_5',
    // Algorithm names are case-insensitive.
    hash: {name: 'Sha-1'}
  };
  var signParams = {
    // Algorithm names are case-insensitive.
    name: 'RSASSA-Pkcs1-v1_5'
  };
  chrome.platformKeys.getKeyPair(
      data.client_2.buffer, keyParams,
      callbackPass(function(publicKey, privateKey) {
        verifySignWithSha1(
            privateKey, signParams, data.signature_client2_sha1_pkcs);
      }));
}

function testSignSha1Client2OnSystemTokenOnly() {
  if (systemTokenEnabled) {
    testSignSha1Client2();
  } else {
    testSignClient2Fails();
  }
}

function verifySignFail(privateKey, signParams) {
  chrome.platformKeys.subtleCrypto()
      .sign(signParams, privateKey, data.raw_data)
      .then(function(signature) {
        fail('sign was expected to fail.');
      }, callbackPass(function(error) {
              assertTrue(error instanceof Error);
              assertEq(
                  'The operation failed for an operation-specific reason',
                  error.message);
            }));
}

// TODO(pmarko,emaxx): Test this by verifying that no private key is returned,
// once that's implemented, see crbug.com/799410.
function testSignFails(cert, spki) {
  var keyParams = {
    name: 'RSASSA-PKCS1-v1_5',
    hash: {name: 'SHA-1'}
  };
  var signParams = {
    name: 'RSASSA-PKCS1-v1_5'
  };
  chrome.platformKeys.getKeyPair(
      cert.buffer, keyParams, callbackPass(function(publicKey, privateKey) {
        verifySignFail(privateKey, signParams);
      }));

  if (spki) {
    chrome.platformKeys.getKeyPairBySpki(
        spki.buffer, keyParams, callbackPass(function(publicKey, privateKey) {
          verifySignFail(privateKey, signParams);
        }));
  }
}

function testSignClient1Fails() {
  testSignFails(data.client_1, data.client_1_spki);
}

function testSignClient2Fails() {
  testSignFails(data.client_2);
}

function testBackgroundNoninteractiveSelect() {
  var details = {interactive: false, request: requestAll};

  chrome.runtime.getBackgroundPage(callbackPass(function(bp) {
    bp.chrome.platformKeys.selectClientCertificates(
      details, callbackPass(function(actualMatches) {
        assertTrue(!bp.chrome.runtime.lastError);
        var expectedCount = systemTokenEnabled ? 3 : 2;
        assertEq(expectedCount, actualMatches.length);
      }));
  }));
}

function testBackgroundInteractiveSelect() {
  var details = {interactive: true, request: requestAll};

  chrome.runtime.getBackgroundPage(callbackPass(function(bp) {
    bp.chrome.platformKeys.selectClientCertificates(
        // callbackPass checks chrome.runtime.lastError and not the error of
        // the background page.
        details, callbackPass(function(actualMatches) {
          assertEq(bp.chrome.runtime.lastError.message,
                   'Interactive calls must happen in the context of a ' +
                       'browser tab or a window.');
          assertEq([], actualMatches);
        }));
  }));
}

function testVerifyTrusted() {
  var details = {
    serverCertificateChain: [data.trusted_l1_leaf_cert.buffer],
    hostname: "l1_leaf"
  };
  chrome.platformKeys.verifyTLSServerCertificate(
      details, callbackPass(function(result) {
        assertTrue(result.trusted);
        assertEq([], result.debug_errors);
      }));
}

function testVerifyTrustedChain() {
  var details = {
    serverCertificateChain:
        [data.trusted_l2_leaf_cert.buffer, data.trusted_l1_interm_cert.buffer],
    hostname: "l2_leaf"
  };
  chrome.platformKeys.verifyTLSServerCertificate(
      details, callbackPass(function(result) {
        assertTrue(result.trusted);
        assertEq([], result.debug_errors);
      }));
}

function testVerifyCommonNameInvalid() {
  var details = {
    serverCertificateChain:
        [data.trusted_l2_leaf_cert.buffer, data.trusted_l1_interm_cert.buffer],
    // Use any hostname not matching the common name 'l2_leaf' of the cert.
    hostname: "abc.example"
  };
  chrome.platformKeys.verifyTLSServerCertificate(
      details, callbackPass(function(result) {
        assertFalse(result.trusted);
        assertEq(["COMMON_NAME_INVALID"], result.debug_errors);
      }));
}

function testVerifyUntrusted() {
  var details = {
    serverCertificateChain: [data.client_1.buffer],
    hostname: "127.0.0.1"
  };
  chrome.platformKeys.verifyTLSServerCertificate(
      details, callbackPass(function(result) {
        assertFalse(result.trusted);
        assertEq(["COMMON_NAME_INVALID", "AUTHORITY_INVALID"],
                 result.debug_errors);
      }));
}

var testSuites = {
  basicTests: function() {
    var tests = [
      testStaticMethods,

      testSignSha1Client2OnSystemTokenOnly,
      // Interactively select all clients to grant permissions for these
      // certificates.
      testInteractiveSelectClient1,
      testInteractiveSelectClient2,
      testInteractiveSelectClient3,

      // In non-interactive calls all certs must be returned now.
      testSelectAllCerts,

      testBackgroundNoninteractiveSelect,
      testBackgroundInteractiveSelect,
      testSelectWithInputClientCerts,
      testSelectCA1Certs,
      testInteractiveSelectNoCerts,
      testMatchResultCA1,
      testMatchResultECDSA,
      testMatchResultRSA,
      testGetKeyPairMissingAlgorithmName,
      testGetKeyPairBySpkiMissingAlgorithmName,
      testGetKeyPairRejectsRSAPSS,
      testGetRsaKeyPairRejectEcdsa,
      testGetEcKeyPairRejectRsa,
      testGetKeyPairBySpkiRsaKeyRejectsEcdsa,
      testGetKeyPairBySpkiEcKeyRejectsRsa,
      testGetRsaKeyPair,
      testGetEcKeyPair,
      testSignNoHash,
      testSignSha1Client1,
      testVerifyTrusted,
      testVerifyTrustedChain,
      testVerifyCommonNameInvalid,
      testVerifyUntrusted,
    ];

    chrome.test.runTests(tests);
  },

  // On interactive selectClientCertificates calls, the simulated user selects
  // client_1, if matching.
  permissionTests: function() {
    var tests = [
      // Without permissions both sign attempts fail.
      testSignClient1Fails,
      testSignClient2Fails,

      // Without permissions, non-interactive select calls return no certs.
      testSelectAllReturnsNoCerts,

      testInteractiveSelectClient1,
      // Now the permission for client_1 is granted.

      // Verify that signing with client_1 is possible and with client_2 still
      // fails.
      testSignSha1Client1,
      testSignClient2Fails,

      // Verify that client_1 can still be selected interactively.
      testInteractiveSelectClient1,

      // Verify that client_1 but not client_2 is selected in non-interactive
      // calls.
      testSelectAllReturnsClient1,
    ];

    chrome.test.runTests(tests);
  },

  managedProfile: function() {
    var tests = [
      // If the profile is managed, the user cannot grant permissions for any
      // certificates.
      testInteractiveSelectNoCerts
    ];
    chrome.test.runTests(tests);
  },

  corporateKeyWithoutPermissionTests: function() {
    var tests = [
      // Directly trying to sign must fail
      testSignClient1Fails,

      // Interactively selecting must not show any cert to the user.
      testInteractiveSelectNoCerts,

      // client_2 is on the system token and is thus implicitly a corporate key.
      // The extension has no access to it.
      testSignClient2Fails,
    ];
    chrome.test.runTests(tests);
  },

  corporateKeyWithPermissionTests: function() {
    var tests = [
      // The extension has non-interactive access to all corporate keys, even
      // without previous additional consent of the user.
      testSignSha1Client1,

      // Interactively selecting for client_1 will work as well.
      testInteractiveSelectClient1,

      // client_2 is on the system token and is thus implicitly a corporate key.
      // The extension has access to it.
      testSignSha1Client2OnSystemTokenOnly,
    ];
    chrome.test.runTests(tests);
  },

  policyDoesGrantAccessToNonCorporateKey: function() {
    // The permission from policy must not affect usage of non-corproate keys.
    var tests = [
      // Attempts to sign must fail.
      testSignClient1Fails,

      // Interactive selection must not prompt the user and not return any
      // certificate.
      testInteractiveSelectNoCerts,
    ];
    chrome.test.runTests(tests);
  },

};

setUp(testSuites[selectedTestSuite]);