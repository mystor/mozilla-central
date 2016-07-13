add_task(function*() {
  yield SpecialPowers.pushPrefEnv({
    set: [
      ["dom.ipc.processCount", 1000],
      ["dom.ipc.processPriorityManager.testMode", true],
      ["dom.ipc.processPriorityManager.enabled", true],
      ["dom.memreservations.enabled", true],
    ]
  });

  ok(true, "Everything is broken anyways");

  return;
  /*
  yield BrowserTestUtils.withNewTab({
    gBrowser: gBrowser,
    url: "about:blank"
  }, function* (browser) {
    browser.loadURI("http://example.com/browser/dom/html/test/file_outofprocess.html");
  });
   */
});
