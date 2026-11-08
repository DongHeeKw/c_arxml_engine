const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
module.exports = async function runScenarios(win, { phase, evidence }) {
    const wc = win.webContents;
    const errors = [];
    wc.on("console-message", (_e, _level, message) => { if (/Uncaught|TypeError|ReferenceError/.test(message)) errors.push(message); });
    const evaluate = (fn, ...args) => wc.executeJavaScript(`(${fn.toString()})(...${JSON.stringify(args)})`, true);
    const wait = async (fn, ...args) => {
      const deadline = Date.now() + 15000;
      while (Date.now() < deadline) {
        if (await evaluate(fn, ...args)) return;
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
      throw new Error(`UI condition timed out: ${fn.toString()}`);
    };
    const click = (selector) => evaluate((s) => { const node = document.querySelector(s); if (!node) throw Error(s); node.click(); }, selector);
    const set = (selector, value) => evaluate((s, v) => {
      const node = document.querySelector(s); if (!node) throw Error(s);
      const prototype = node.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;
      Object.getOwnPropertyDescriptor(prototype, "value").set.call(node, v);
      node.dispatchEvent(new Event(node.tagName === "SELECT" ? "change" : "input", { bubbles: true }));
    }, selector, value);
    const field = (label, value) => evaluate((text, v) => {
      const node = [...document.querySelectorAll("label")].find((item) => item.querySelector("span")?.textContent === text)?.querySelector("input");
      if (!node) throw Error(text);
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value").set.call(node,v);
      node.dispatchEvent(new Event("input",{ bubbles: true }));
    }, label, value);
    const screenshot = async (name) => { await fs.writeFile(path.join(evidence, name + ".png"), (await wc.capturePage()).toPNG()); };
    try {
      await win.setContentSize(1440, 1080);
      await wait(() => document.body.textContent.includes("Engine connected"));
      if (phase === "import") {
        await screenshot("01-empty-workspace");
        await click('.page-heading .primary-button');
        await wait(() => document.querySelectorAll(".file-card").length === 1);
        await field("CAN channel for these files", "Body CAN");
        await evaluate(() => [...document.querySelectorAll("button")].find((node) => node.textContent === "Save network sources").click());
        await wait(() => document.querySelector('[aria-label="Channel"]')?.value === "Body CAN");
        checks.push("real chooser → preview → explicit channel → SQLite import");
      } else {
        await wait(() => [...document.querySelector('[aria-label="Channel"]').options].some((item) => item.value === "Body CAN"));
        await set('[aria-label="Channel"]', "Body CAN");
        checks.push("fresh Electron and engine process restored persisted sources");
      }
      await set('[aria-label="Target ECU"]', "BDC");
      await wait(() => document.querySelectorAll(".message-row").length === 3);
      const domNames = await evaluate(() => [...document.querySelectorAll(".message-title strong")].map((item) => item.textContent));
      const stored = await evaluate(() => window.studio.request("network.ecu", { channel: "Body CAN", ecu: "BDC" }));
      assert.deepEqual(domNames, stored.messages.map((message) => message.name));
      assert.equal(await evaluate(() => document.querySelectorAll(".peer-node").length), 3);
      checks.push("renderer messages and topology match persisted engine facts");
      await screenshot(phase === "import" ? "02-network-topology" : "07-restored-network");
      await click('[aria-label="Show messages with CLUSTER"]');
      await wait(() => document.querySelectorAll(".message-row").length === 1);
      assert.equal(await evaluate(() => document.querySelector(".message-title strong").textContent), "DoorStatus");
      checks.push("multicast sibling excluded: CLUSTER link shows DoorStatus, not GatewayState");
      await screenshot("03-focused-connection");
      await set('[aria-label="Connected ECU"]', "");
      await set('[aria-label="Search messages"]', "Temperature");
      await wait(() => document.querySelectorAll(".message-row").length === 1 && document.querySelector(".signal-inspector h3")?.textContent === "CabinTemperature");
      await set('[aria-label="Search messages"]', "no-such-signal");
      await wait(() => document.querySelectorAll(".message-row").length === 0);
      await click(".empty-results button");
      await wait(() => document.querySelectorAll(".message-row").length === 3);
      await click('[aria-label="Pause diagram motion"]');
      assert.equal(await evaluate(() => getComputedStyle(document.querySelector(".wire-flow")).animationName), "none");
      checks.push("search, empty results, clear filters and motion pause");
      await wc.debugger.attach("1.3");
      await wc.debugger.sendCommand("Emulation.setEmulatedMedia", { features: [{ name: "prefers-reduced-motion", value: "reduce" }] });
      await click('[aria-label="Resume diagram motion"]');
      assert.equal(await evaluate(() => getComputedStyle(document.querySelector(".wire-flow")).animationName), "none");
      wc.debugger.detach();
      checks.push("OS reduced-motion preference disables topology animation");
      await evaluate(() => document.querySelector('[aria-label="Show messages with HVAC"]').focus());
      await wc.sendInputEvent({ type: "keyDown", keyCode: "Return" });
      await wc.sendInputEvent({ type: "keyUp", keyCode: "Return" });
      await wait(() => document.querySelector('[aria-label="Show messages with HVAC"]')?.getAttribute("aria-pressed") === "true");
      checks.push("keyboard Enter selects a focused ECU link");
      await set('[aria-label="Connected ECU"]', "");
      await win.setContentSize(1040, 900);
      await new Promise((resolve) => setTimeout(resolve, 200));
      assert.ok(await evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      await screenshot("04-compact-workspace");
      await win.setContentSize(1440, 1080);
      await click('.app-nav nav button:nth-child(4)');
      await wait(() => !document.querySelector(".review-panel").hidden);
      assert.ok(await evaluate(() => document.querySelector(".review-panel").textContent.includes("DBC_UNSUPPORTED_DECLARATION")));
      assert.ok(await evaluate(() => document.querySelector(".readiness-banner").textContent.includes("BLOCKED")));
      await screenshot("05-source-review");
      checks.push("persisted unsupported diagnostics and unresolved generation blocker remain visible");
      await click('.app-nav nav button:nth-child(3)');
      if (phase === "import") {
        await click(".srs-actions button");
        await wait(() => document.querySelectorAll(".srs-panel .error-message").length > 0);
        const before = await evaluate(() => window.studio.request("project.status"));
        assert.equal(before.projectOpen, false);
        await screenshot("06-validation-errors");
        for (const [name, value] of [["Project name", "Body controller"],["MCU vendor","Synthetic"],["MCU family","Test"],["MCU part number","Fixture-1"]]) await field(name,value);
        await click(".srs-actions button");
        await wait(() => document.querySelector(".srs-actions span")?.textContent === "Project definition saved");
        checks.push("invalid form never commits; corrected project definition saves through engine");
      } else {
        assert.equal(await evaluate(() => [...document.querySelectorAll("label")].find((node) => node.querySelector("span")?.textContent === "Project name").querySelector("input").value), "Body controller");
        checks.push("project form rehydrated from SQLite after full process restart");
      }
      assert.deepEqual(errors, []);
      await fs.writeFile(path.join(evidence, `${phase}-results.json`), JSON.stringify({ phase, checks, consoleErrors: errors }, null, 2));
      console.log(`PASS ${phase}: ${checks.length} Electron interaction checks`);
      return checks;
    } catch (error) {
      await screenshot("failure").catch(() => {}); throw error;
    }
};
