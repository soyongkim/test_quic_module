// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** The data structure the message pipe sends and receives. */
class MessageData {
  constructor() {
    /**
     * The id of the message, this uniquely identifies a message
     * and should only appear on the sent message and a response to that
     * message.
     *
     * @type {number}
     */
    this.messageId;
    /**
     * The message type. Indicates the structure of the data in
     * `message` and is set to special reserved strings when the message is
     * an generated messaged used to communicate between message pipe
     * instances.
     *
     * @type {string}
     */
    this.type;
    /**
     * The message being sent through the pipe, the structure of
     * the object sent is implied by the type of the message.
     *
     * @type {!Object}
     */
    this.message;
  }
}

/**
 * The Object placed in MessageData.message (and thrown by the Promise returned
 * by sendMessage) if an exception is caught on the receiving end.
 * @typedef {{
 *     name: string,
 *     message: string,
 *     stack: string,
 * }}
 */
let GenericErrorResponse;

/**
 * Error object allowing attributes to be undefined.
 * @typedef {{
 *    name: (string|undefined),
 *    message: (string|undefined),
 *    stack: (string|undefined),
 * }}
 */
let DefensiveError;

/**
 * To handle generic errors such as `DOMException` not being an `Error`
 * defensively assign '' if the attribute is undefined. Without explicitly
 * extracting fields, `Errors` are sent as `{}` across the pipe.
 * @param {!DefensiveError} error
 * @return {!GenericErrorResponse}
 */
function serializeError(error) {
  return {
    message: error.message || '',
    name: error.name || '',
    stack: error.stack || ''
  };
}

/**
 * The type of a message handler function which gets called when the message
 * pipe receives a message.
 *
 * @typedef {function(!Object): (!Object|undefined|!Promise<!Object|undefined>)}
 */
let MessageHandler;

/**
 * Creates a new JavaScript native Promise and captures its resolve and reject
 * callbacks. The promise, resolve, and reject are available as properties.
 * Inspired by goog.promise.NativeResolver.
 */
class NativeResolver {
  constructor() {
    /** @type {function(!Object): void} */
    this.resolve;
    /** @type {function(!Object): void} */
    this.reject;

    /** @type {!Promise<!Object>} */
    this.promise = new Promise((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
    });
  }
}

/**
 * A simplified "assert" that casts away null types. Assumes preconditions that
 * satisfy the assert have already been checked. Inspired by
 * webui/resources/js/assert/assert.js. However, this file is used (and tested)
 * verbatim in multiple repositories with different dependency management, so
 * that's not used directly. TODO(b/150650426): consolidate this better.
 *
 * @template T
 * @param {?T|undefined} condition
 * @return {T} A non-null |condition|.
 * @closurePrimitive {asserts.truthy}
 * @suppress {reportUnknownTypes} because T is not sufficiently constrained.
 */
/* #export */ function assertCast(condition) {
  if (!condition) {
    throw new Error('Failed assertion');
  }
  return condition;
}

/**
 * Enum for reserved message types used in generated messages.
 *
 * @enum {string}
 */
const ReservedMessageTypes = {
  /**
   * Indicates a autogenerated response message for a previously received
   * message.
   */
  RESPONSE_TYPE: '___response',
  /**
   * Indicates a autogenerated error message for a previously received
   * message.
   */
  ERROR_TYPE: '___error'
};

/**
 * Checks if a provided message type indicates a generated message.
 *
 * @param {string} messageType
 * @return {boolean}
 */
function isGeneratedMessage(messageType) {
  // Any message type with three underscores before it should only be used
  // in generated messages.
  return messageType.substr(0, 3) === '___';
}

/**
 * Checks a message type is not reserved by generated messages, if it is, throws
 * a error indicating this to the user.
 *
 * @param {string} messageType
 */
function throwIfReserved(messageType) {
  if (isGeneratedMessage(messageType)) {
    throw new Error(`Unexpected reserved message type: '${messageType}'`);
  }
}

/**
 * The message pipe allows two windows to communicate in 1 direction without
 * having to handle the internals. The caller can send messages to the other
 * window and receive async responses.
 */
/* #export */ class MessagePipe {
  /**
   * Constructs a new message pipe to the `target` window which has the
   * `targetOrigin` origin.
   *
   * @param {string} targetOrigin
   * @param {!Window=} target If not specified, the document tree will be
   *     queried for a iframe with src `targetOrigin` to target.
   * @param {boolean=} rethrowErrors
   */
  constructor(targetOrigin, target, rethrowErrors = true) {
    if (!target) {
      const frame = /** @type {!HTMLIFrameElement} */ (
          document.querySelector(`iframe[src^='${targetOrigin}']`));
      if (!frame || !frame.contentWindow) {
        throw new Error('Unable to locate target content window.');
      }
      target = assertCast(frame.contentWindow);
    }

    /** @private @const {!Window} */
    this.target_ = target;

    /** @private @const {string} */
    this.targetOrigin_ = targetOrigin;

    /**
     * If true any errors thrown in a handler during message handling will be
     * thrown again in addition to being sent over the pipe to the message
     * sender. true by default.
     *
     * @type {boolean}
     */
    this.rethrowErrors = rethrowErrors;

    /**
     * Client error logger. Mockable for tests that check for errors. This is
     * only used to log errors generated from handlers. Logging occurs on both
     * sides of the message pipe if rethrowErrors is set, otherwise only on
     * the side that sent the message.
     */
    this.logClientError = (/** * */ object) =>
        console.error(JSON.stringify(object));

    /**
     * Maps a message type to a message handler, a function which takes in
     * the message and returns a response message or a promise which resolves
     * with a response message.
     *
     * @private @const {!Map<string, !MessageHandler>}
     */
    this.messageHandlers_ = new Map();

    /**
     * Maps a message id to a resolver.
     *
     * @private @const {!Map<number, !NativeResolver>}
     */
    this.pendingMessages_ = new Map();

    /**
     * The id the next message the object sends will have.
     *
     * @private
     */
    this.nextMessageId_ = 0;

    /**
     * The message listener we attach to the window. We need a reference to the
     * function for later removal.
     *
     * @private @const {function(!Event): void}
     */
    this.messageListener_ = (m) => this.receiveMessage_(m);

    // Make sure we aren't trying to send messages to ourselves.
    console.assert(this.target_ !== window, 'target !== window');

    window.addEventListener('message', this.messageListener_);
  }

  /**
   * Registers a handler to be called when a message of type `messageType` is
   * received. The return value of this handler will automatically be sent to
   * the message source as a response message. If the handler should throw an
   * error while handling a message, the error message will be caught and sent
   * to the message source automatically.
   * NOTE: The message type can not be prefixed with 3 underscores as that is
   * reserved for generated messages. i.e `___hello` is disallowed.
   *
   * @param {string} messageType
   * @param {!MessageHandler} handler
   */
  registerHandler(messageType, handler) {
    throwIfReserved(messageType);
    if (this.messageHandlers_.has(messageType)) {
      throw new Error(`A handler already exists for ${messageType}`);
    }

    this.messageHandlers_.set(messageType, handler);
  }

  /**
   * Wraps `sendMessageImpl()` catching errors from the target context to throw
   * more useful errors with the current context stacktrace attached.
   *
   * @param {string} messageType
   * @param {!Object=} message
   * @return {!Promise<!Object>}
   */
  async sendMessage(messageType, message = {}) {
    try {
      return await this.sendMessageImpl(messageType, message);
    } catch (/** @type {!GenericErrorResponse} */ errorResponse) {
      // Create an error with the name of the IPC function invoked, append the
      // stacktrace from the target context (origin of the error) with the
      // stacktrace of the current context.
      const error = new Error(`${messageType}: ${errorResponse.message}`);
      error.name = errorResponse.name || 'Unknown Error';
      error.stack +=
          `\nError from ${this.targetOrigin_}\n${errorResponse.stack}`;
      // TODO(b/156205603): use internal `chrome.crashReportPrivate.reportError`
      // to log this error.
      throw error;
    }
  }

  /**
   * Sends a message to the target window and return a Promise that will resolve
   * on response. If the target handler does not send a response the promise
   * will resolve with a empty object.
   *
   * @private
   * @param {string} messageType
   * @param {!Object=} message
   * @return {!Promise<!Object>}
   */
  async sendMessageImpl(messageType, message = {}) {
    throwIfReserved(messageType);

    const messageId = this.nextMessageId_++;
    const resolver = new NativeResolver();
    this.pendingMessages_.set(messageId, resolver);

    this.postToTarget_(messageType, message, messageId);

    return resolver.promise;
  }

  /**
   * Removes all listeners this object attaches to window in preparation for
   * destruction.
   */
  detach() {
    window.removeEventListener('message', this.messageListener_);
  }

  /**
   * Handles a message which represents the targets response to a previously
   * sent message.
   *
   * @private
   * @param {string} messageType
   * @param {!Object} message
   * @param {number} messageId
   */
  handleMessageResponse_(messageType, message, messageId) {
    const {RESPONSE_TYPE, ERROR_TYPE} = ReservedMessageTypes;
    const resolver = this.pendingMessages_.get(messageId);

    if (messageType === RESPONSE_TYPE) {
      resolver.resolve(message);
    } else if (messageType === ERROR_TYPE) {
      this.logClientError(message);
      resolver.reject(message);
    } else {
      console.error(`Response for message ${
          messageId} received with invalid message type ${messageType}`);
    }
    this.pendingMessages_.delete(messageId);
  }

  /**
   * Calls the relevant handler for a received message and generates the right
   * response message to send back to the source.
   *
   * @private
   * @param {string} messageType
   * @param {!Object} message
   * @param {number} messageId
   * @return {!Promise<void>}
   */
  async callHandlerForMessageType_(messageType, message, messageId) {
    const {RESPONSE_TYPE, ERROR_TYPE} = ReservedMessageTypes;
    /** @type {!Object|undefined} */
    let response;
    /** @type {?DefensiveError} */
    let error = null;
    /** @type {boolean} */
    let sawError = false;

    try {
      response = await this.messageHandlers_.get(messageType)(message);
    } catch (/** @type {!DefensiveError} */ err) {
      // If an error happened capture the error and send it back.
      sawError = true;
      error = err;
      response = serializeError(err);
    }
    this.postToTarget_(
        sawError ? ERROR_TYPE : RESPONSE_TYPE, response, messageId);

    if (sawError && this.rethrowErrors) {
      // Rethrow the error so the current frame has visibility on its handler
      // failures.
      this.logClientError(error);
      throw error;
    }
  }

  /**
   * @private
   * @param {!Event} event
   */
  receiveMessage_(event) {
    const e = /** @type {!MessageEvent<!MessageData>} */ (event);

    // Ignore message events missing a type.
    if (typeof e.data !== 'object' || !e.data
        || typeof e.data.type !== 'string') {
      return;
    }
    const {messageId, type, message} = e.data;
    const {ERROR_TYPE} = ReservedMessageTypes;

    // Ignore any messages that are not from the target origin unless we are
    // explicitly accepting messages from any origin.
    if (e.origin !== this.targetOrigin_ && this.targetOrigin_ !== '*') {
      return;
    }

    // The case that the message is a response to a previously sent message.
    if (isGeneratedMessage(type) && this.pendingMessages_.has(messageId)) {
      this.handleMessageResponse_(type, message, messageId);
      return;
    }

    if (isGeneratedMessage(type)) {
      // Currently all generated messages are only sent in a response, so should
      // have been handled above.
      console.error(`Response with type ${type} for unknown message received.`);
      return;
    }

    if (!this.messageHandlers_.has(type)) {
      // If there is no listener for this event send a error message to source.
      const error =
          new Error(`No handler registered for message type '${type}'`);
      const errorResponse = serializeError(error);
      this.postToTarget_(ERROR_TYPE, errorResponse, messageId);
      return;
    }

    this.callHandlerForMessageType_(type, message, messageId);
  }

  /**
   * @private
   * @param {string} messageType
   * @param {!Object|undefined} message
   * @param {number} messageId
   */
  postToTarget_(messageType, message, messageId) {
    const messageWrapper = {
      messageId,
      type: messageType,
      message: message || {}
    };
    // The next line should probably be passing a transfer argument, but that
    // causes Chrome to send a "null" message. The transfer seems to work
    // without the third argument (but inefficiently, perhaps).
    this.target_.postMessage(messageWrapper, this.targetOrigin_);
  }
}