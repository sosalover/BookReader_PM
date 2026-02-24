// PocketMage V3.0
// @Ashtf 2025

#include <globals.h>

static constexpr const char* TAG = "MAIN"; // TODO: Come up with a better tag

//        .o.       ooooooooo.   ooooooooo.    .oooooo..o  //
//       .888.      `888   `Y88. `888   `Y88. d8P'    `Y8  //
//      .8"888.      888   .d88'  888   .d88' Y88bo.       //
//     .8' `888.     888ooo88P'   888ooo88P'   `"Y8888o.   //
//    .88ooo8888.    888          888              `"Y88b  //
//   .8'     `888.   888          888         oo     .d8P  //
//  o88o     o8888o o888o        o888o        8""88888P'   //


// ADD E-INK HANDLER APP SCRIPTS HERE
void applicationEinkHandler() {
  #if OTA_APP
    einkHandler_APP(); // OTA_APP: entry point
  #endif
  // OTA_APP: Remove switch statement
  #if !OTA_APP // POCKETMAGE_OS
  switch (CurrentAppState) {
    case HOME:
      einkHandler_HOME();
      break;
    case TXT:
      einkHandler_TXT_NEW();
      break;
    case FILEWIZ:
      einkHandler_FILEWIZ();
      break;
    case TASKS:
      einkHandler_TASKS();
      break;
    case SETTINGS:
      einkHandler_settings();
      break;
    case USB_APP:
      einkHandler_USB();
      break;
    case CALENDAR:
      einkHandler_CALENDAR();
      break;
    case LEXICON:
      einkHandler_LEXICON();
      break;
    case JOURNAL:
      einkHandler_JOURNAL();
      break;
    case APPLOADER:
      einkHandler_APPLOADER();
      break;
    // ADD APP CASES HERE
    default:
      einkHandler_HOME();
      break;
  }
  #endif // POCKETMAGE_OS
}

// ADD PROCESS/KEYBOARD APP SCRIPTS HERE
void processKB() {
  // Check for USB KB
  KB().checkUSBKB();

  // Example OTA APP 
  // Displays a progress bar and then reboots to PocketMage OS
  // Remove this when making a real OTA APP + uncomment processKB_APP();
  #if OTA_APP
    // OTA apps fully own behavior
    processKB_APP();
    return;
  #endif
  // OTA_APP: Remove switch statement
  #if !OTA_APP // POCKETMAGE_OS
  switch (CurrentAppState) {
    case HOME:
      processKB_HOME();
      break;
    case TXT:
      processKB_TXT_NEW();
      break;
    case FILEWIZ:
      processKB_FILEWIZ();
      break;
    case TASKS:
      processKB_TASKS();
      break;
    case SETTINGS:
      processKB_settings();
      break;
    case USB_APP:
      processKB_USB();
      break;
    case CALENDAR:
      processKB_CALENDAR();
      break;
    case LEXICON:
      processKB_LEXICON();
      break;
    case JOURNAL:
      processKB_JOURNAL();
      break;
    case APPLOADER:
      processKB_APPLOADER();
      break;
    // ADD APP CASES HERE
    default:
      processKB_HOME();
      break;
  }
  #endif // POCKETMAGE_OS
}

//  ooo        ooooo       .o.       ooooo ooooo      ooo  //
//  `88.       .888'      .888.      `888' `888b.     `8'  //
//   888b     d'888      .8"888.      888   8 `88b.    8   //
//   8 Y88. .P  888     .8' `888.     888   8   `88b.  8   //
//   8  `888'   888    .88ooo8888.    888   8     `88b.8   //
//   8    Y     888   .8'     `888.   888   8       `888   //
//  o8o        o888o o88o     o8888o o888o o8o        `8   //

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////|
// SETUP
void setup() {
  PocketMage_INIT();
  #if OTA_APP
  APP_INIT();
  #endif
}

// Keyboard / OLED Loop
void loop() {
  static int i = 0;
  #if !OTA_APP // POCKETMAGE_OS
    if (!noTimeout)  checkTimeout();
    if (DEBUG_VERBOSE) printDebug();

    if (DEBUG_VERBOSE) PowerSystem.printDiagnostics();
  #endif

  updateBattState();
  processKB();

  // Yield to watchdog
  vTaskDelay(50 / portTICK_PERIOD_MS);
  yield();
}

// E-Ink Loop
void einkHandler(void* parameter) {
  vTaskDelay(pdMS_TO_TICKS(250)); 
  for (;;) {
    applicationEinkHandler();

    vTaskDelay(pdMS_TO_TICKS(50));
    yield();
  }
}