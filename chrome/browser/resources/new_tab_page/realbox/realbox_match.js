// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import './realbox_icon.js';
import './realbox_action.js';
import 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.m.js';
import 'chrome://resources/cr_elements/cr_icons_css.m.js';
import 'chrome://resources/cr_elements/hidden_style_css.m.js';

import {html, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {loadTimeData} from '../i18n_setup.js';
import {decodeString16, mojoTimeTicks} from '../utils.js';
import {RealboxBrowserProxy} from './realbox_browser_proxy.js';

// clang-format off
/**
 * Bitmap used to decode the value of realbox.mojom.ACMatchClassification style
 * field.
 * See components/omnibox/browser/autocomplete_match.h.
 * @enum {number}
 */
const ACMatchClassificationStyle = {
  NONE: 0,
  URL:   1 << 0,  // A URL.
  MATCH: 1 << 1,  // A match for the user's search term.
  DIM:   1 << 2,  // A "helper text".
};
// clang-format on

// Displays an autocomplete match similar to those in the Omnibox.
class RealboxMatchElement extends PolymerElement {
  static get is() {
    return 'ntp-realbox-match';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      //========================================================================
      // Public properties
      //========================================================================

      /**
       * Element's 'aria-label' attribute.
       * @type {string}
       */
      ariaLabel: {
        type: String,
        computed: `computeAriaLabel_(match.a11yLabel)`,
        reflectToAttribute: true,
      },

      /**
       * Whether the match features an image (as opposed to an icon or favicon).
       * @type {boolean}
       */
      hasImage: {
        type: Boolean,
        computed: `computeHasImage_(match)`,
        reflectToAttribute: true,
      },

      /**
       * @type {!realbox.mojom.AutocompleteMatch}
       */
      match: {
        type: Object,
      },

      /**
       * Index of the match in the autocomplete result. Used to inform embedder
       * of events such as deletion, click, etc.
       * @type {number}
       */
      matchIndex: {
        type: Number,
        value: -1,
      },

      //========================================================================
      // Private properties
      //========================================================================

      /**
       * @type {boolean}
       * @private
       */
      actionIsVisible_: {
        type: Boolean,
        computed: `computeActionIsVisible_(match)`,
      },

      /**
       * Rendered match contents based on autocomplete provided styling.
       * @type {string}
       * @private
       */
      contentsHtml_: {
        type: String,
        computed: `computeContentsHtml_(match)`,
      },

      /**
       * Rendered match description based on autocomplete provided styling.
       * @type {string}
       * @private
       */
      descriptionHtml_: {
        type: String,
        computed: `computeDescriptionHtml_(match)`,
      },

      /**
       * Remove button's 'aria-label' attribute.
       * @type {string}
       * @private
       */
      removeButtonAriaLabel_: {
        type: String,
        computed: `computeRemoveButtonAriaLabel_(match.removeButtonA11yLabel)`,
      },

      /**
       * @type {string}
       * @private
       */
      removeButtonTitle_: {
        type: String,
        value: () => loadTimeData.getString('removeSuggestion'),
      },

      /**
       * Used to separate the contents from the description.
       * @type {string}
       * @private
       */
      separatorText_: {
        type: String,
        computed: `computeSeparatorText_(match)`,
      },

      /**
       * Rendered tail suggest common prefix.
       * @type {string}
       * @private
       */
      tailSuggestPrefix_: {
        type: String,
        computed: `computeTailSuggestPrefix_(match)`,
      },
    };
  }

  constructor() {
    super();
    /** @private {realbox.mojom.PageHandlerRemote} */
    this.pageHandler_ = RealboxBrowserProxy.getInstance().handler;
  }

  ready() {
    super.ready();

    this.addEventListener('click', (event) => this.onMatchClick_(event));
    this.addEventListener('focusin', (event) => this.onMatchFocusin_(event));
  }

  //============================================================================
  // Event handlers
  //============================================================================

  /**
   * @param {!Event} e Event
   * containing index of the match that was removed as well as modifier key
   * presses.
   * @private
   */
  executeAction_(e) {
    this.pageHandler_.executeAction(
        this.matchIndex, mojoTimeTicks(Date.now()), e.button || 0, e.altKey,
        e.ctrlKey, e.metaKey, e.shiftKey);
  }

  /**
   * @param {!Event} e
   * @private
   */
  onActionClick_(e) {
    this.executeAction_(e);

    e.preventDefault();   // Prevents default browser action (navigation).
    e.stopPropagation();  // Prevents <iron-selector> from selecting the match.
  }

  /**
   * @param {!Event} e
   * @private
   */
  onActionKeyDown_(e) {
    if (e.key && (e.key === 'Enter' || e.key === ' ')) {
      this.onActionClick_(e);
    }
  }

  /**
   * @param {!Event} e
   * @private
   */
  onMatchClick_(e) {
    if (e.button > 1) {
      // Only handle main (generally left) and middle button presses.
      return;
    }

    this.dispatchEvent(new CustomEvent('match-click', {
      bubbles: true,
      composed: true,
      detail: {index: this.matchIndex, event: e},
    }));

    e.preventDefault();   // Prevents default browser action (navigation).
    e.stopPropagation();  // Prevents <iron-selector> from selecting the match.
  }

  /**
   * @param {!Event} e
   * @private
   */
  onMatchFocusin_(e) {
    this.dispatchEvent(new CustomEvent('match-focusin', {
      bubbles: true,
      composed: true,
      detail: this.matchIndex,
    }));
  }

  /**
   * @param {!Event} e
   * @private
   */
  onRemoveButtonClick_(e) {
    if (e.button !== 0) {
      // Only handle main (generally left) button presses.
      return;
    }
    this.dispatchEvent(new CustomEvent('match-remove', {
      bubbles: true,
      composed: true,
      detail: this.matchIndex,
    }));

    e.preventDefault();   // Prevents default browser action (navigation).
    e.stopPropagation();  // Prevents <iron-selector> from selecting the match.
  }

  /**
   * @param {!Event} e
   * @private
   */
  onRemoveButtonMouseDown_(e) {
    e.preventDefault();  // Prevents default browser action (focus).
  }

  //============================================================================
  // Helpers
  //============================================================================


  /**
   * @return {string}
   * @private
   */
  computeAriaLabel_() {
    if (!this.match) {
      return '';
    }
    return decodeString16(this.match.a11yLabel);
  }

  /**
   * @return {string}
   * @private
   */
  computeContentsHtml_() {
    if (!this.match) {
      return '';
    }
    const match = this.match;
    // `match.answer.firstLine` is generated by appending an optional additional
    // text from the answer's first line to `match.contents`, making the latter
    // a prefix of the former. Thus `match.answer.firstLine` can be rendered
    // using the markup in `match.contentsClass` which contains positions in
    // `match.contents` and the markup to be applied to those positions.
    // See //chrome/browser/ui/webui/realbox/realbox_handler.cc
    const matchContents =
        match.answer ? match.answer.firstLine : match.contents;
    return match.swapContentsAndDescription ?
        this.renderTextWithClassifications_(
                decodeString16(match.description), match.descriptionClass)
            .innerHTML :
        this.renderTextWithClassifications_(
                decodeString16(matchContents), match.contentsClass)
            .innerHTML;
  }

  /**
   * @return {string}
   * @private
   */
  computeDescriptionHtml_() {
    if (!this.match) {
      return '';
    }
    const match = this.match;
    if (match.answer) {
      return decodeString16(match.answer.secondLine);
    }
    return match.swapContentsAndDescription ?
        this.renderTextWithClassifications_(
                decodeString16(match.contents), match.contentsClass)
            .innerHTML :
        this.renderTextWithClassifications_(
                decodeString16(match.description), match.descriptionClass)
            .innerHTML;
  }

  /**
   * @return {string}
   * @private
   */
  computeTailSuggestPrefix_() {
    if (!this.match || !this.match.tailSuggestCommonPrefix) {
      return '';
    }
    const prefix = decodeString16(this.match.tailSuggestCommonPrefix);
    // Replace last space with non breaking space since spans collapse
    // trailing white spaces and the prefix always ends with a white space.
    if (prefix.slice(-1) === ' ') {
      return prefix.slice(0, -1) + '\u00A0';
    }
    return prefix;
  }

  /**
   * @return {boolean}
   * @private
   */
  computeHasImage_() {
    return this.match && !!this.match.imageUrl;
  }

  /**
   * @return {boolean}
   * @private
   */
  computeActionIsVisible_() {
    return this.match && !!this.match.action;
  }

  /**
   * @return {string}
   * @private
   */
  computeRemoveButtonAriaLabel_() {
    if (!this.match) {
      return '';
    }
    return decodeString16(this.match.removeButtonA11yLabel);
  }

  /**
   * @return {string}
   * @private
   */
  computeSeparatorText_() {
    return this.match && decodeString16(this.match.description) ?
        loadTimeData.getString('realboxSeparator') :
        '';
  }

  /**
   * Decodes the ACMatchClassificationStyle enteries encoded in the given
   * realbox.mojom.ACMatchClassification style field, maps each entry to a CSS
   * class and returns them.
   * @param {number} style
   * @return {!Array<string>}
   */
  convertClassificationStyleToCSSClasses_(style) {
    const classes = [];
    if (style & ACMatchClassificationStyle.DIM) {
      classes.push('dim');
    }
    if (style & ACMatchClassificationStyle.MATCH) {
      classes.push('match');
    }
    if (style & ACMatchClassificationStyle.URL) {
      classes.push('url');
    }
    return classes;
  }

  /**
   * @param {string} text
   * @param {!Array<string>} classes
   * @return {!Element}
   */
  createSpanWithClasses_(text, classes) {
    const span = document.createElement('span');
    if (classes.length) {
      span.classList.add(...classes);
    }
    span.textContent = text;
    return span;
  }

  /**
   * Renders |text| based on the given realbox.mojom.ACMatchClassification(s)
   * Each classification contains an 'offset' and an encoded list of styles for
   * styling a substring starting with the 'offset' and ending with the next.
   * @param {string} text
   * @param {!Array<!realbox.mojom.ACMatchClassification>} classifications
   * @return {!Element} A <span> with <span> children for each styled substring.
   */
  renderTextWithClassifications_(text, classifications) {
    return classifications
        .map(({offset, style}, index) => {
          const next = classifications[index + 1] || {offset: text.length};
          const subText = text.substring(offset, next.offset);
          const classes = this.convertClassificationStyleToCSSClasses_(style);
          return this.createSpanWithClasses_(subText, classes);
        })
        .reduce((container, currentElement) => {
          container.appendChild(currentElement);
          return container;
        }, document.createElement('span'));
  }
}

customElements.define(RealboxMatchElement.is, RealboxMatchElement);
