#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <OneButton.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

// --- Hardware Definitions ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RST 21
#define OLED_SDA 17
#define OLED_SCL 18
#define BTN_1_PIN 0

#define M_LEFT 2
#define M_TOP 2
#define M_RIGHT 125
#define M_BOTTOM 61

// --- Global HTML Constants ---
const char INDEX_HTML_TOP[] PROGMEM = R"html(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><style>body { font-family: sans-serif; padding: 20px; background: #f0f0f0; }.card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); max-width: 400px; margin: auto; }h1 { color: #333; font-size: 24px; margin-bottom: 5px; }h3 { margin: 20px 0 10px; color: #666; border-bottom: 1px solid #ddd; padding-bottom: 5px; }input, select { width: 100%; padding: 12px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box; font-size: 16px; }input[type='button'] { background: #007bff; color: white; border: none; font-weight: bold; cursor: pointer; }#prog-cont { width: 100%; background: #ddd; border-radius: 5px; display: none; margin-top: 15px; }#prog-bar { width: 0%; height: 25px; background: #28a745; border-radius: 5px; text-align: center; color: white; line-height: 25px; font-size: 14px; transition: width 0.2s; }#status { margin-top: 15px; font-weight: bold; text-align: center; }.file-list { list-style: none; padding: 0; }.file-item { display: flex; justify-content: space-between; padding: 10px; border-bottom: 1px solid #eee; font-size: 14px; }.del-btn { color: #dc3545; text-decoration: none; font-weight: bold; }</style></head><body><div class="card"><h1>EsBook32 Librarian</h1><form id="uploadForm"><h3>Upload New Book</h3><select id="author_pick" name="author_pick"><option value="">-- New Author --</option>)html";
const char INDEX_HTML_MID[] PROGMEM = R"html(</select><input type="text" id="author_new" name="author_new" placeholder="Or type new author name"><input type="text" id="title" name="title" placeholder="Book Title"><input type="file" id="fileInput" name="upload" accept=".txt"><input type="button" value="Upload" onclick="send()"></form><div id="prog-cont"><div id="prog-bar">0%</div></div><div id="status"></div><h3>Manage Library</h3><div class="file-list">)html";
const char INDEX_HTML_BOTTOM[] PROGMEM = R"html(</div></div><script>function send() { var f = document.getElementById('fileInput'); if (f.files.length === 0) { alert('Select a file!'); return; } var fd = new FormData(); fd.append('author_pick', document.getElementById('author_pick').value); fd.append('author_new', document.getElementById('author_new').value); fd.append('title', document.getElementById('title').value); fd.append('upload', f.files[0]); var x = new XMLHttpRequest(); x.open('POST', '/upload', true); x.upload.onprogress = function(e) { if (e.lengthComputable) { document.getElementById('prog-cont').style.display = 'block'; var p = Math.round((e.loaded / e.total) * 100); var b = document.getElementById('prog-bar'); b.style.width = p + '%'; b.innerHTML = p + '%'; } }; x.onload = function() { location.reload(); }; x.send(fd); }</script></body></html>)html";

OneButton btn1(BTN_1_PIN, true, true);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
Preferences prefs;
WebServer server(80);

const char* apSSID = "EsBook32_Portal";
IPAddress apIP(192, 168, 4, 1);
File uploadFile;

enum AppState { MENU_MAIN, MENU_SETTINGS, MENU_DISPLAY, MENU_RESET_PROGRESS, MENU_DELETE_SELECT, MENU_DELETE_CONFIRM, MENU_PAUSE, READING, WIFI_PORTAL };
AppState currentState = MENU_MAIN;

#define MAX_FILES 30
String menuItems[MAX_FILES];
int numMenuItems = 0, menuCursorIndex = 0;
String currentPath = "/";
String bookToDelete = "";
int scrollX = 0, footerScrollX = 0;
unsigned long lastScrollTime = 0, lastFooterTime = 0, scrollDelayStart = 0;
const int MARQUEE_GAP = 30;
bool scrollComplete = false;

int currentBrightness = 4;
bool isDarkMode = true, isFlipped = false;
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 120000;

unsigned long pageStartTime = 0, sessionStartTime = 0;
uint32_t sessionStartByte = 0;
int currentSessionWPM = 0, sessionElapsedMins = 0, autoSaveCounter = 0;

String currentBook = "";
uint32_t currentBookSize = 0;
#define MAX_PAGES 500
uint32_t pageStarts[MAX_PAGES];
int currentPage = 0;

void buildMainMenu();
void buildSettingsMenu();
void buildDisplayMenu();
void buildResetMenu();
void buildDeleteMenu();
void buildPauseMenu();
void drawMenu();
void executeMenuSelection();
void applySettings();
void printStatusBar(bool showProgress);
void renderPage();
void changePage(int direction);
void saveProgress();
void loadProgress();
void goToSleep();
void resetActivityTimer();
void startWiFiPortal();
void updateWiFiDisplay();
void stopWiFiPortal();
void drawAnimatedSplash();
void deleteBookAndCleanup(String fullPath);
uint8_t getLatin1(uint8_t c);
void sortMenu(int startIdx, int endIdx);

void setup() {
  Serial.begin(115200);
  pinMode(36, OUTPUT); digitalWrite(36, LOW);
  pinMode(37, OUTPUT); digitalWrite(37, HIGH);
  delay(100);
  btn1.setClickMs(190); btn1.setPressMs(600);
  Wire.begin(OLED_SDA, OLED_SCL); Wire.setClock(200000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE); display.setTextWrap(false); display.cp437(true); 
  LittleFS.begin(true);
  prefs.begin("esbook", false);
  currentBrightness = prefs.getInt("bright", 4);
  isDarkMode = prefs.getBool("dark", true);
  isFlipped = prefs.getBool("flip", false);
  currentBook = prefs.getString("lastBook", "");
  applySettings(); drawAnimatedSplash(); resetActivityTimer();
  btn1.attachClick([]() {
    resetActivityTimer();
    if (currentState == READING) changePage(1);
    else if (currentState == MENU_DELETE_CONFIRM) { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else if (currentState != WIFI_PORTAL) {
      menuCursorIndex++; if (menuCursorIndex >= numMenuItems) menuCursorIndex = 0;
      scrollX = 0; scrollComplete = false; scrollDelayStart = millis(); drawMenu();
    }
  });
  btn1.attachDoubleClick([]() {
    resetActivityTimer();
    if (currentState == READING) changePage(-1);
    else if (currentState == WIFI_PORTAL) { stopWiFiPortal(); currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else if (currentState == MENU_PAUSE) { currentState = READING; pageStartTime = millis(); renderPage(); }
    else if (currentState == MENU_DELETE_CONFIRM) { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else {
      menuCursorIndex--; if (menuCursorIndex < 0) menuCursorIndex = numMenuItems - 1;
      scrollX = 0; scrollComplete = false; scrollDelayStart = millis(); drawMenu();
    }
  });
  btn1.attachLongPressStart([]() {
    resetActivityTimer();
    if (currentState == READING) {
      unsigned long now = millis();
      sessionElapsedMins = (now - sessionStartTime) / 60000;
      if (sessionElapsedMins < 1) sessionElapsedMins = 1; 
      uint32_t totalBytesRead = pageStarts[currentPage] - sessionStartByte;
      float totalWords = totalBytesRead / 5.0; 
      currentSessionWPM = (int)(totalWords / (float)sessionElapsedMins);
      currentState = MENU_PAUSE; buildPauseMenu();
    }
    else if (currentState == MENU_DELETE_CONFIRM) { deleteBookAndCleanup(bookToDelete); currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else if (currentState != WIFI_PORTAL) executeMenuSelection();
  });
  if (currentBook != "" && currentBook != "none" && LittleFS.exists(currentBook)) { 
    loadProgress(); sessionStartByte = pageStarts[currentPage]; sessionStartTime = millis();
    currentState = READING; pageStartTime = millis(); renderPage(); 
  } else { buildMainMenu(); }
}

void loop() {
  btn1.tick();
  if (currentState != READING && currentState != WIFI_PORTAL && numMenuItems > 0 && !scrollComplete) {
    String currentText = menuItems[menuCursorIndex];
    int limit = (currentState == MENU_MAIN) ? 95 : 110;
    int tW = currentText.length() * 6;
    if (tW > limit) {
      if (millis() - scrollDelayStart > 1500) {
        if (millis() - lastScrollTime > 50) {
          scrollX++; if (scrollX >= (tW + MARQUEE_GAP)) { scrollX = (tW + MARQUEE_GAP); scrollComplete = true; }
          lastScrollTime = millis(); drawMenu();
        }
      }
    }
  }
  if (currentState == WIFI_PORTAL) {
    server.handleClient();
    if (millis() - lastFooterTime > 40) {
      footerScrollX++; if (footerScrollX >= (28 * 6 + MARQUEE_GAP)) footerScrollX = 0;
      lastFooterTime = millis(); updateWiFiDisplay();
    }
    resetActivityTimer();
  }
  if (millis() - lastActivityTime > SLEEP_TIMEOUT) goToSleep();
}

void resetActivityTimer() { lastActivityTime = millis(); }

uint8_t getLatin1(uint8_t c) {
  static uint8_t state = 0;
  if (state == 0) { if (c < 0x80) return c; state = c; return 0; }
  else if (state == 0xC2) { state = 0; return c; }
  else if (state == 0xC3) { state = 0; return c | 0x40; }
  else if (state == 0xE2) { if (c == 0x80) { state = 0x01; return 0; } state = 0; return 0; }
  else if (state == 0x01) {
    state = 0; 
    if (c == 0x98 || c == 0x99 || c == 0x9B) return '\''; 
    if (c == 0x9C || c == 0x9D || c == 0x9F) return '"';  
    if (c == 0x93 || c == 0x94) return '-';               
    if (c == 0xA6) return '.';                            
    return '\''; 
  }
  state = 0; return 0;
}

void deleteBookAndCleanup(String fullPath) {
  if (!fullPath.endsWith(".txt")) fullPath += ".txt";
  if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
  LittleFS.remove(fullPath);
  String p = fullPath; p.replace(".txt", ".pos");
  if (LittleFS.exists(p)) LittleFS.remove(p);
}

void sortMenu(int startIdx, int endIdx) {
  for (int i = startIdx; i < endIdx - 1; i++) {
    for (int j = i + 1; j < endIdx; j++) {
      String a = menuItems[i], b = menuItems[j];
      bool aF = a.startsWith("[+] "), bF = b.startsWith("[+] "), doSwap = false;
      if (!aF && bF) doSwap = true;
      else if (aF == bF) {
        String cA = aF ? a.substring(4) : a; String cB = bF ? b.substring(4) : b;
        cA.toLowerCase(); cB.toLowerCase(); if (cA.compareTo(cB) > 0) doSwap = true;
      }
      if (doSwap) { menuItems[i] = b; menuItems[j] = a; }
    }
  }
}

void buildMainMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false; scrollDelayStart = millis();
  File root = LittleFS.open("/"); File file = root.openNextFile();
  if (currentPath == "/") {
    while(file && numMenuItems < MAX_FILES - 2) {
      String fn = String(file.name()); if (fn.startsWith("/")) fn = fn.substring(1);
      if (fn.endsWith(".txt")) {
        int tidx = fn.indexOf('~');
        if (tidx > 0) {
          String vF = "[+] " + fn.substring(0, tidx);
          bool ex = false; for(int i=0; i<numMenuItems; i++) if(menuItems[i]==vF){ex=true; break;}
          if(!ex) menuItems[numMenuItems++] = vF;
        } else menuItems[numMenuItems++] = fn.substring(0, fn.length() - 4);
      }
      file = root.openNextFile();
    }
    sortMenu(0, numMenuItems); menuItems[numMenuItems++] = "-> Settings";
  } else {
    menuItems[numMenuItems++] = "[<-- Back]";
    String auth = currentPath.substring(1);
    while(file && numMenuItems < MAX_FILES) {
      String fn = String(file.name()); if (fn.startsWith("/")) fn = fn.substring(1);
      if (fn.endsWith(".txt") && fn.startsWith(auth + "~")) menuItems[numMenuItems++] = fn.substring(auth.length() + 1, fn.length() - 4);
      file = root.openNextFile();
    }
    sortMenu(1, numMenuItems);
  }
  drawMenu();
}

void buildSettingsMenu() {
  numMenuItems = 0; menuCursorIndex = 0;
  menuItems[numMenuItems++] = "Book Upload"; menuItems[numMenuItems++] = "Display Setup";
  menuItems[numMenuItems++] = "Progress Reset"; menuItems[numMenuItems++] = "Delete Book";
  menuItems[numMenuItems++] = "Power Off"; menuItems[numMenuItems++] = "< Back to Books";
  drawMenu();
}

void buildDisplayMenu() {
  numMenuItems = 0; menuCursorIndex = 0;
  menuItems[numMenuItems++] = "Brightness: " + String(currentBrightness);
  menuItems[numMenuItems++] = isDarkMode ? "Mode: DARK" : "Mode: LIGHT";
  menuItems[numMenuItems++] = isFlipped ? "Orient: FLIPPED" : "Orient: NORMAL";
  menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void buildPauseMenu() {
  numMenuItems = 0; menuCursorIndex = 0;
  menuItems[numMenuItems++] = "Continue Reading";
  menuItems[numMenuItems++] = "Save & Exit";
  drawMenu();
}

void buildDeleteMenu() {
  numMenuItems = 0; menuCursorIndex = 0;
  File root = LittleFS.open("/"); File entry = root.openNextFile();
  while(entry && numMenuItems < MAX_FILES - 1) {
    String fn = String(entry.name()); if (fn.startsWith("/")) fn = fn.substring(1);
    if(fn.endsWith(".txt")) { String d = fn.substring(0, fn.length() - 4); d.replace("~", " - "); menuItems[numMenuItems++] = d; }
    entry = root.openNextFile();
  }
  sortMenu(0, numMenuItems); menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void buildResetMenu() {
  numMenuItems = 0; menuCursorIndex = 0;
  File root = LittleFS.open("/"); File file = root.openNextFile();
  while(file && numMenuItems < MAX_FILES - 1){
    String fn = String(file.name()); if (fn.startsWith("/")) fn = fn.substring(1);
    if(fn.endsWith(".pos")) { String d = fn.substring(0, fn.length() - 4); d.replace("~", " - "); menuItems[numMenuItems++] = d; }
    file = root.openNextFile();
  }
  sortMenu(0, numMenuItems); menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void drawMenu() {
  display.clearDisplay(); display.setTextSize(1);
  if (currentState == MENU_DELETE_CONFIRM) {
    display.setCursor(M_LEFT, M_TOP+8); display.println("   !!! DELETE !!!");
    display.drawLine(M_LEFT, M_TOP+18, M_RIGHT, M_TOP+18, WHITE);
    display.setCursor(M_LEFT, M_TOP+28); display.println("Remove permanently:");
    String d = bookToDelete; d.replace("~", " - "); if (d.startsWith("/")) d = d.substring(1);
    display.setCursor(M_LEFT, M_TOP+38); display.println(d);
    display.setCursor(M_LEFT, M_BOTTOM-8); display.println("Click:NO  Hold:YES");
    display.display(); return;
  }
  int startY = (currentState == MENU_MAIN || currentState == MENU_PAUSE) ? M_TOP+20 : M_TOP+11;
  int dOff = (currentState == MENU_MAIN || currentState == MENU_PAUSE) ? 3 : 4;
  if (currentState == MENU_MAIN || currentState == MENU_PAUSE) {
    printStatusBar(currentState == MENU_PAUSE); display.setCursor(M_LEFT, M_TOP+10);
    String h = currentPath; h.replace("~", " - ");
    display.print(currentPath == "/" ? "--- Library ---" : "--- " + h + " ---");
    if (currentState == MENU_PAUSE) { 
      display.setCursor(M_LEFT, M_BOTTOM - 8); 
      display.print("Sesh.: "); display.print(sessionElapsedMins); display.print("m : "); 
      display.print(currentSessionWPM); display.println(" WPM");
    }
  } else {
    display.setCursor(M_LEFT, M_TOP);
    if (currentState == MENU_SETTINGS) display.println("[ Settings ]");
    else if (currentState == MENU_DISPLAY) display.println("[ Display ]");
    else if (currentState == MENU_RESET_PROGRESS) display.println("[ Reset ]");
    else if (currentState == MENU_DELETE_SELECT) display.println("[ Delete Book ]");
  }
  for (int i = 0; i < numMenuItems; i++) {
    int drawY = startY + (i * 10) - (menuCursorIndex > dOff ? (menuCursorIndex - dOff) * 10 : 0);
    // FIXED: Corrected vertical boundary for non-pause menus
    int limitY = (currentState == MENU_PAUSE) ? M_BOTTOM - 10 : M_BOTTOM - 1;
    if (drawY >= startY && drawY <= limitY) {
      String it = menuItems[i]; if (i == menuCursorIndex) { display.setCursor(M_LEFT, drawY); display.print(">"); }
      int tX = M_LEFT + (it.startsWith("[+] ") ? 32 : 8);
      if (it.startsWith("[+] ")) { display.setCursor(M_LEFT + 8, drawY); display.print("[+] "); it = it.substring(4); }
      int lim = (M_RIGHT - tX); int tW = it.length() * 6;
      display.setCursor(tX - (i == menuCursorIndex && tW > lim ? scrollX : 0), drawY);
      for(int j=0; j<it.length(); j++) { uint8_t c = getLatin1((uint8_t)it[j]); if(c > 0) display.write(c); }
    }
  }
  display.display();
}

void executeMenuSelection() {
  String sel = menuItems[menuCursorIndex];
  if (currentState == MENU_MAIN) {
    if (sel == "[<-- Back]") { currentPath = "/"; buildMainMenu(); }
    else if (sel.startsWith("[+] ")) { currentPath = "/" + sel.substring(4); buildMainMenu(); }
    else if (sel == "-> Settings") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else {
      currentBook = (currentPath == "/") ? "/" + sel + ".txt" : "/" + currentPath.substring(1) + "~" + sel + ".txt";
      prefs.putString("lastBook", currentBook); loadProgress(); 
      sessionStartByte = pageStarts[currentPage]; sessionStartTime = millis();
      currentState = READING; pageStartTime = millis(); renderPage();
    }
  } else if (currentState == MENU_PAUSE) {
    if (sel == "Continue Reading") { currentState = READING; pageStartTime = millis(); renderPage(); }
    else { saveProgress(); currentBook = ""; prefs.putString("lastBook", "none"); currentState = MENU_MAIN; buildMainMenu(); }
  } else if (currentState == MENU_SETTINGS) {
    if (sel == "Book Upload") startWiFiPortal();
    else if (sel == "Display Setup") { currentState = MENU_DISPLAY; buildDisplayMenu(); }
    else if (sel == "Progress Reset") { currentState = MENU_RESET_PROGRESS; buildResetMenu(); }
    else if (sel == "Delete Book") { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else if (sel == "Power Off") goToSleep();
    else if (sel == "< Back to Books") { currentState = MENU_MAIN; buildMainMenu(); }
  } else if (currentState == MENU_DISPLAY) {
    if (sel.indexOf("Mode") != -1) { isDarkMode = !isDarkMode; prefs.putBool("dark", isDarkMode); applySettings(); buildDisplayMenu(); }
    else if (sel.indexOf("Orient") != -1) { isFlipped = !isFlipped; prefs.putBool("flip", isFlipped); applySettings(); buildDisplayMenu(); }
    else if (sel.indexOf("Brightness") != -1) { currentBrightness++; if(currentBrightness > 4) currentBrightness = 1; prefs.putInt("bright", currentBrightness); applySettings(); buildDisplayMenu(); }
    else { currentState = MENU_SETTINGS; buildSettingsMenu(); }
  } else if (currentState == MENU_RESET_PROGRESS) {
    if (sel == "< Back" || sel == "No progress found") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else { String t = sel; t.replace(" - ", "~"); LittleFS.remove("/" + t + ".pos"); buildResetMenu(); }
  } else if (currentState == MENU_DELETE_SELECT) {
    if (sel == "< Back" || sel == "Library empty") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else { bookToDelete = "/" + sel; bookToDelete.replace(" - ", "~"); if (!bookToDelete.endsWith(".txt")) bookToDelete += ".txt"; currentState = MENU_DELETE_CONFIRM; drawMenu(); }
  }
}

void startWiFiPortal() {
  currentState = WIFI_PORTAL; footerScrollX = 0; WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); WiFi.softAP(apSSID);
  server.on("/del", HTTP_GET, []() {
    String f = server.arg("f"); if (f != "") LittleFS.remove("/" + f);
    server.sendHeader("Location", "/"); server.send(303);
  });
  server.on("/", HTTP_GET, []() {
    String h = INDEX_HTML_TOP; File root = LittleFS.open("/"); File file = root.openNextFile(); String auths[MAX_FILES]; int nA = 0;
    while(file) {
      String fn = String(file.name()); if(fn.startsWith("/")) fn = fn.substring(1);
      int tidx = fn.indexOf('~');
      if (tidx > 0 && nA < MAX_FILES) {
        String a = fn.substring(0, tidx); bool ex = false; for (int i=0; i<nA; i++) if (auths[i] == a) {ex=true; break;}
        if (!ex) { auths[nA++] = a; h += "<option value='" + a + "'>" + a + "</option>"; }
      }
      file = root.openNextFile();
    }
    h += INDEX_HTML_MID; root = LittleFS.open("/"); file = root.openNextFile();
    while(file) {
      String fn = String(file.name()); if (fn.startsWith("/")) fn = fn.substring(1);
      if (fn.endsWith(".txt")) {
        h += "<div class='file-item'><span>" + fn + "</span><a class='del-btn' href='/del?f=" + fn + "'>[Delete]</a></div>";
      }
      file = root.openNextFile();
    }
    h += INDEX_HTML_BOTTOM; server.send(200, "text/html", h);
  });
  server.on("/upload", HTTP_POST, []() { server.send(200); }, []() {
    HTTPUpload& u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
      String a = server.arg("author_pick"); if (a == "") a = server.arg("author_new"); a.trim();
      String t = server.arg("title"); t.trim();
      String fN = (t == "") ? u.filename : t; if (fN.endsWith(".txt")) fN = fN.substring(0, fN.length() - 4);
      fN = (a != "") ? a + "~" + fN + ".txt" : fN + ".txt";
      uploadFile = LittleFS.open("/" + fN, "w");
    } else if (u.status == UPLOAD_FILE_WRITE && uploadFile) uploadFile.write(u.buf, u.currentSize);
    else if (u.status == UPLOAD_FILE_END && uploadFile) uploadFile.close();
  });
  server.begin();
}

void updateWiFiDisplay() {
  display.clearDisplay(); display.setCursor(M_LEFT, M_TOP); display.println("--- Upload Portal ---"); display.drawLine(M_LEFT, M_TOP+8, M_RIGHT, M_TOP+8, WHITE);
  display.setCursor(M_LEFT, M_TOP+18); display.print("SSID: "); display.println(apSSID); display.setCursor(M_LEFT, M_TOP+30); display.print("IP:   "); display.println(apIP.toString());
  display.drawLine(M_LEFT, M_BOTTOM-12, M_RIGHT, M_BOTTOM-12, WHITE);
  String tk = "Double-click to close portal"; int tW = tk.length() * 6;
  display.setCursor(M_LEFT - footerScrollX, M_BOTTOM-8); display.print(tk);
  display.setCursor(M_LEFT - footerScrollX + tW + MARQUEE_GAP, M_BOTTOM-8); display.print(tk); display.display();
}

void stopWiFiPortal() { server.stop(); WiFi.softAPdisconnect(true); }

void drawAnimatedSplash() {
  String v = "v5.4.1 [UI FIX]";
  for (int i = 0; i <= 100; i += 4) {
    display.clearDisplay(); display.setTextSize(2); display.setCursor(22, 18); display.print("EsBook32");
    display.drawRect(23, 45, 82, 8, WHITE); display.fillRect(25, 47, map(i,0,100,0,78), 4, WHITE);
    display.setTextSize(1); display.setCursor(64 - (v.length() * 3), 36); display.print(v); display.display(); delay(45);
  }
}

void applySettings() {
  display.setRotation(isFlipped ? 2 : 0); uint8_t c = (currentBrightness == 1) ? 32 : (currentBrightness == 2) ? 96 : (currentBrightness == 3) ? 160 : 255;
  display.ssd1306_command(SSD1306_SETCONTRAST); display.ssd1306_command(c);
  if(isDarkMode) display.ssd1306_command(SSD1306_NORMALDISPLAY); else display.ssd1306_command(SSD1306_INVERTDISPLAY);
}

void printStatusBar(bool showProgress) {
  display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(M_LEFT, M_TOP); display.print("EsB32");
  if (showProgress && currentBookSize > 0) { int p = (pageStarts[currentPage] * 100) / currentBookSize; display.setCursor(58, M_TOP); display.print(p); display.print("%"); }
  display.drawLine(M_LEFT, M_TOP+8, M_RIGHT, M_TOP+8, WHITE);
}

void loadProgress() { String p = currentBook; p.replace(".txt", ".pos"); File f = LittleFS.open(p, "r"); pageStarts[0] = f ? f.readString().toInt() : 0; currentPage = 0; if(f) f.close(); }
void saveProgress() { if (currentBook == "" || currentBook == "none") return; String p = currentBook; p.replace(".txt", ".pos"); File f = LittleFS.open(p, "w"); if(f) { f.print(pageStarts[currentPage]); f.close(); } }

void changePage(int direction) {
  if (direction == 1) {
    if (pageStarts[currentPage + 1] >= currentBookSize) {
      display.clearDisplay(); display.setCursor(20, 25); display.println("Book Finished!"); display.display(); delay(2000);
      String p = currentBook; p.replace(".txt", ".pos"); LittleFS.remove(p);
      currentBook = ""; prefs.putString("lastBook", "none"); currentState = MENU_MAIN; buildMainMenu(); return;
    } else if (pageStarts[currentPage + 1] > pageStarts[currentPage]) currentPage++;
  } else if (direction == -1 && currentPage > 0) currentPage--;
  autoSaveCounter++; if (autoSaveCounter >= 10) { saveProgress(); autoSaveCounter = 0; }
  pageStartTime = millis(); renderPage();
}

void renderPage() {
  File f = LittleFS.open(currentBook, "r"); if (!f) return;
  currentBookSize = f.size(); f.seek(pageStarts[currentPage]);
  display.clearDisplay(); display.setTextSize(1);
  int x = M_LEFT, y = M_TOP; uint32_t lineStart = pageStarts[currentPage]; pageStarts[currentPage+1] = 0;
  String word = ""; uint32_t wordStartPos = f.position();
  while (f.available()) {
    uint32_t charPos = f.position(); uint8_t raw = f.read(); uint8_t c = getLatin1(raw);
    if (c == 0) continue; 
    if (c != ' ' && c != '\n' && c != '\r') { if (word.length() == 0) wordStartPos = charPos; word += (char)c; }
    else {
      if (word.length() > 0) {
        int wW = word.length() * 6; if (x + wW > M_RIGHT) { x = M_LEFT; y += 8; }
        if (y > M_BOTTOM - 8) { lineStart = wordStartPos; break; }
        display.setCursor(x, y); display.print(word); x += wW; word = "";
      }
      if (c == ' ') { if (x + 6 <= M_RIGHT) { display.write(' '); x += 6; } else { x = M_LEFT; y += 8; } }
      else if (c == '\n') { x = M_LEFT; y += 8; }
      if (y > M_BOTTOM - 8) { lineStart = f.position(); break; }
    }
  }
  if (currentBookSize > 0) {
    int barWidth = map(pageStarts[currentPage], 0, currentBookSize, 0, M_RIGHT - M_LEFT);
    display.drawLine(M_LEFT, 63, M_LEFT + barWidth, 63, WHITE);
  }
  display.display(); f.close(); pageStarts[currentPage + 1] = lineStart;
}

void goToSleep() { saveProgress(); display.clearDisplay(); display.setCursor(20, 30); display.println("Sleeping"); display.display(); delay(1000); display.ssd1306_command(SSD1306_DISPLAYOFF); esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_1_PIN, 0); esp_deep_sleep_start(); }