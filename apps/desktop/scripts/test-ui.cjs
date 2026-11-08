const { app, dialog } = require("electron");
const runScenarios = require("./ui-scenarios.cjs");
app.setPath("userData", process.env.STUDIO_UI_USER_DATA);
// Native chooser alone is substituted; the Electron/preload/engine path is real.
dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [process.env.STUDIO_UI_FIXTURE] });
const timeout = setTimeout(() => { console.error("Electron UI test timed out"); app.exit(1); }, 75000);
app.on("browser-window-created", (_event, win) => {
  win.webContents.once("did-finish-load", async () => {
    try {
      await runScenarios(win, { phase: process.env.STUDIO_UI_PHASE, evidence: process.env.STUDIO_UI_EVIDENCE });
      clearTimeout(timeout); app.quit();
    } catch(error) { console.error(error.stack); clearTimeout(timeout); app.exit(1); }
  });
});
require("../electron/main.cjs");
