// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import { AppearanceBrowserProxy, AppearanceBrowserProxyImpl,HomeUrlInputElement, SettingsAppearancePageElement} from 'chrome://settings/settings.js';
import { assertEquals,assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';
// clang-format on

class TestAppearanceBrowserProxy extends TestBrowserProxy implements
    AppearanceBrowserProxy {
  private defaultZoom_: number = 1;
  private isChildAccount_: boolean = false;
  private isHomeUrlValid_: boolean = true;

  constructor() {
    super([
      'getDefaultZoom',
      'getThemeInfo',
      'isChildAccount',
      'useDefaultTheme',
      'useSystemTheme',
      'validateStartupPage',
    ]);
  }

  getDefaultZoom() {
    this.methodCalled('getDefaultZoom');
    return Promise.resolve(this.defaultZoom_);
  }

  getThemeInfo(themeId: string) {
    this.methodCalled('getThemeInfo', themeId);
    return Promise.resolve({
      id: '',
      name: 'Sports car red',
      shortName: '',
      description: '',
      version: '',
      mayDisable: false,
      enabled: false,
      isApp: false,
      offlineEnabled: false,
      optionsUrl: '',
      permissions: [],
      hostPermissions: [],
    });
  }

  isChildAccount() {
    this.methodCalled('isChildAccount');
    return this.isChildAccount_;
  }

  useDefaultTheme() {
    this.methodCalled('useDefaultTheme');
  }

  useSystemTheme() {
    this.methodCalled('useSystemTheme');
  }

  setDefaultZoom(defaultZoom: number) {
    this.defaultZoom_ = defaultZoom;
  }

  setIsChildAccount(isChildAccount: boolean) {
    this.isChildAccount_ = isChildAccount;
  }

  validateStartupPage(url: string) {
    this.methodCalled('validateStartupPage', url);
    return Promise.resolve(this.isHomeUrlValid_);
  }

  setValidStartupPageResponse(isValid: boolean) {
    this.isHomeUrlValid_ = isValid;
  }
}

let appearancePage: SettingsAppearancePageElement;
let appearanceBrowserProxy: TestAppearanceBrowserProxy;

function createAppearancePage() {
  appearanceBrowserProxy.reset();
  document.body.innerHTML = '';

  appearancePage = document.createElement('settings-appearance-page');
  appearancePage.set('prefs', {
    autogenerated: {
      theme: {
        policy: {
          color: {
            value: 0,
          }
        }
      }
    },
    extensions: {
      theme: {
        id: {
          value: '',
        },
        use_system: {
          value: false,
        },
      },
    },
  });

  appearancePage.set('pageVisibility', {
    setWallpaper: true,
  });

  document.body.appendChild(appearancePage);
  flush();
}

suite('AppearanceHandler', function() {
  setup(function() {
    appearanceBrowserProxy = new TestAppearanceBrowserProxy();
    AppearanceBrowserProxyImpl.setInstance(appearanceBrowserProxy);
    createAppearancePage();
  });

  teardown(function() {
    appearancePage.remove();
  });

  const THEME_ID_PREF = 'prefs.extensions.theme.id.value';

  // <if expr="is_linux and not chromeos and not lacros">
  const USE_SYSTEM_PREF = 'prefs.extensions.theme.use_system.value';

  test('useDefaultThemeLinux', function() {
    assertFalse(!!appearancePage.get(THEME_ID_PREF));
    assertFalse(appearancePage.get(USE_SYSTEM_PREF));
    // No custom nor system theme in use; "USE CLASSIC" should be hidden.
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useDefault'));

    appearancePage.set(USE_SYSTEM_PREF, true);
    flush();
    // If the system theme is in use, "USE CLASSIC" should show.
    assertTrue(!!appearancePage.shadowRoot!.querySelector('#useDefault'));

    appearancePage.set(USE_SYSTEM_PREF, false);
    appearancePage.set(THEME_ID_PREF, 'fake theme id');
    flush();

    // With a custom theme installed, "USE CLASSIC" should show.
    const button =
        appearancePage.shadowRoot!.querySelector<HTMLElement>('#useDefault');
    assertTrue(!!button);

    button!.click();
    return appearanceBrowserProxy.whenCalled('useDefaultTheme');
  });

  test('useSystemThemeLinux', function() {
    assertFalse(!!appearancePage.get(THEME_ID_PREF));
    appearancePage.set(USE_SYSTEM_PREF, true);
    flush();
    // The "USE GTK+" button shouldn't be showing if it's already in use.
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useSystem'));

    appearanceBrowserProxy.setIsChildAccount(true);
    appearancePage.set(USE_SYSTEM_PREF, false);
    flush();
    // Child account users have their own theme and can't use GTK+ theme.
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useDefault'));
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useSystem'));
    // If there's no "USE" buttons, the container should be hidden.
    assertTrue(
        appearancePage.shadowRoot!
            .querySelector<HTMLElement>('#themesSecondaryActions')!.hidden);

    appearanceBrowserProxy.setIsChildAccount(false);
    appearancePage.set(THEME_ID_PREF, 'fake theme id');
    flush();
    // If there's "USE" buttons again, the container should be visible.
    assertTrue(!!appearancePage.shadowRoot!.querySelector('#useDefault'));
    assertFalse(
        appearancePage.shadowRoot!
            .querySelector<HTMLElement>('#themesSecondaryActions')!.hidden);

    const button =
        appearancePage.shadowRoot!.querySelector<HTMLElement>('#useSystem');
    assertTrue(!!button);

    button!.click();
    return appearanceBrowserProxy.whenCalled('useSystemTheme');
  });
  // </if>

  // <if expr="not is_linux or chromeos_ash or chromeos_lacros">
  test('useDefaultTheme', function() {
    assertFalse(!!appearancePage.get(THEME_ID_PREF));
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useDefault'));

    appearancePage.set(THEME_ID_PREF, 'fake theme id');
    flush();

    // With a custom theme installed, "RESET TO DEFAULT" should show.
    const button =
        appearancePage.shadowRoot!.querySelector<HTMLElement>('#useDefault');
    assertTrue(!!button);

    button!.click();
    return appearanceBrowserProxy.whenCalled('useDefaultTheme');
  });

  test('useDefaultThemeWithPolicy', function() {
    const POLICY_THEME_COLOR_PREF = 'prefs.autogenerated.theme.policy.color';
    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useDefault'));

    // "Reset to default" button doesn't appear as result of a policy theme.
    appearancePage.set(POLICY_THEME_COLOR_PREF, {controlledBy: 'PRIMARY_USER'});
    flush();

    assertFalse(!!appearancePage.shadowRoot!.querySelector('#useDefault'));

    // Unset policy theme and set custom theme to get button to show.
    appearancePage.set(POLICY_THEME_COLOR_PREF, {});
    appearancePage.set(THEME_ID_PREF, 'fake theme id');
    flush();

    let button =
        appearancePage.shadowRoot!.querySelector<HTMLElement>('#useDefault');
    assertTrue(!!button);

    // Clicking "Reset to default" button when a policy theme is applied
    // causes the managed theme dialog to appear.
    appearancePage.set(POLICY_THEME_COLOR_PREF, {controlledBy: 'PRIMARY_USER'});
    flush();

    button =
        appearancePage.shadowRoot!.querySelector<HTMLElement>('#useDefault');
    assertTrue(!!button);
    assertEquals(
        null, appearancePage.shadowRoot!.querySelector('managed-dialog'));

    button!.click();
    flush();

    assertFalse(
        appearancePage.shadowRoot!.querySelector('managed-dialog')!.hidden);
  });
  // </if>

  test('default zoom handling', function() {
    function getDefaultZoomText() {
      const zoomLevel = appearancePage.$.zoomLevel;
      return zoomLevel.options[zoomLevel.selectedIndex]!.textContent!.trim();
    }

    return appearanceBrowserProxy.whenCalled('getDefaultZoom')
        .then(function() {
          assertEquals('100%', getDefaultZoomText());

          appearanceBrowserProxy.setDefaultZoom(2 / 3);
          createAppearancePage();
          return appearanceBrowserProxy.whenCalled('getDefaultZoom');
        })
        .then(function() {
          assertEquals('67%', getDefaultZoomText());

          appearanceBrowserProxy.setDefaultZoom(11 / 10);
          createAppearancePage();
          return appearanceBrowserProxy.whenCalled('getDefaultZoom');
        })
        .then(function() {
          assertEquals('110%', getDefaultZoomText());

          appearanceBrowserProxy.setDefaultZoom(1.7499999999999);
          createAppearancePage();
          return appearanceBrowserProxy.whenCalled('getDefaultZoom');
        })
        .then(function() {
          assertEquals('175%', getDefaultZoomText());
        });
  });

  test('show home button toggling', function() {
    assertFalse(!!appearancePage.shadowRoot!.querySelector('.list-frame'));
    appearancePage.set('prefs', {
      autogenerated: {theme: {policy: {color: {value: 0}}}},
      browser: {show_home_button: {value: true}},
      extensions: {theme: {id: {value: ''}}},
    });
    flush();

    assertTrue(!!appearancePage.shadowRoot!.querySelector('.list-frame'));
  });
});

suite('HomeUrlInput', function() {
  let homeUrlInput: HomeUrlInputElement;

  setup(function() {
    appearanceBrowserProxy = new TestAppearanceBrowserProxy();
    AppearanceBrowserProxyImpl.setInstance(appearanceBrowserProxy);
    document.body.innerHTML = '';

    homeUrlInput = document.createElement('home-url-input');
    homeUrlInput.set(
        'pref', {type: chrome.settingsPrivate.PrefType.URL, value: 'test'});

    document.body.appendChild(homeUrlInput);
    flush();
  });

  test('home button urls', function() {
    assertFalse(homeUrlInput.invalid);
    assertEquals(homeUrlInput.value, 'test');

    homeUrlInput.value = '@@@';
    appearanceBrowserProxy.setValidStartupPageResponse(false);
    homeUrlInput.$.input.fire('input');

    return appearanceBrowserProxy.whenCalled('validateStartupPage')
        .then(function(url) {
          assertEquals(homeUrlInput.value, url);
          flush();
          assertEquals(homeUrlInput.value, '@@@');  // Value hasn't changed.
          assertTrue(homeUrlInput.invalid);

          // Should reset to default value on change event.
          homeUrlInput.$.input.fire('change');
          flush();
          assertEquals(homeUrlInput.value, 'test');
        });
  });
});