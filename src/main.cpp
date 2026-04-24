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

// --- Strict 2-Pixel Margin Constants ---
#define M_LEFT 2
#define M_TOP 2
#define M_RIGHT 125
#define M_BOTTOM 61

// --- Global HTML Constants ---
const char INDEX_HTML_TOP[] PROGMEM = R"html(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><style>body { font-family: sans-serif; padding: 20px; background: #f0f0f0; }.card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); max-width: 400px; margin: auto; }h1 { color: #333; font-size: 24px; margin-bottom: 5px; }h3 { margin: 20px 0 10px; color: #666; border-bottom: 1px solid #ddd; padding-bottom: 5px; }input, select { width: 100%; padding: 12px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box; font-size: 16px; }input[type='button'] { background: #007bff; color: white; border: none; font-weight: bold; cursor: pointer; }#prog-cont { width: 100%; background: #ddd; border-radius: 5px; display: none; margin-top: 15px; }#prog-bar { width: 0%; height: 25px; background: #28a745; border-radius: 5px; text-align: center; color: white; line-height: 25px; font-size: 14px; transition: width 0.2s; }#status { margin-top: 15px; font-weight: bold; text-align: center; }.blurb { font-size: 14px; color: #777; margin-bottom: 20px; line-height: 1.4; }</style></head><body><div class="card"><h1>EsBook32 Librarian</h1><div class="blurb">Note: This e-reader currently supports <b>.txt</b> files only.</div><form id="uploadForm"><h3>Author (Virtual Folder)</h3><select id="author_pick" name="author_pick"><option value="">-- New Author --</option>)html";

const char INDEX_HTML_BOTTOM[] PROGMEM = R"html(</select><input type="text" id="author_new" name="author_new" placeholder="Or type new author name"><h3>Book Details</h3><input type="text" id="title" name="title" placeholder="Book Title"><input type="file" id="fileInput" name="upload" accept=".txt"><input type="button" value="Upload to EsBook32" onclick="send()"></form><div id="prog-cont"><div id="prog-bar">0%</div></div><div id="status"></div></div><script>function send() { var f = document.getElementById('fileInput'); if (f.files.length === 0) { alert('Select a file!'); return; } var fd = new FormData(); fd.append('author_pick', document.getElementById('author_pick').value); fd.append('author_new', document.getElementById('author_new').value); fd.append('title', document.getElementById('title').value); fd.append('upload', f.files[0]); var x = new XMLHttpRequest(); x.open('POST', '/upload', true); x.upload.onprogress = function(e) { if (e.lengthComputable) { document.getElementById('prog-cont').style.display = 'block'; var p = Math.round((e.loaded / e.total) * 100); var b = document.getElementById('prog-bar'); b.style.width = p + '%'; b.innerHTML = p + '%'; } }; x.onload = function() { if (x.status === 200) { document.getElementById('status').innerHTML = 'SUCCESS! Dbl-Click PRG to exit.'; document.getElementById('status').style.color = '#28a745'; } else { document.getElementById('status').innerHTML = 'ERROR.'; document.getElementById('status').style.color = '#dc3545'; } }; x.send(fd); }</script></body></html>)html";

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
int numMenuItems = 0;
int menuCursorIndex = 0;
String currentPath = "/";
String bookToDelete = "";

int scrollX = 0;
int footerScrollX = 0;
unsigned long lastScrollTime = 0;
unsigned long lastFooterTime = 0;
unsigned long scrollDelayStart = 0;
const int MARQUEE_GAP = 30;
bool scrollComplete = false;

int currentBrightness = 4;
bool isDarkMode = true;
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 120000;

unsigned long pageStartTime = 0;
float avgSecondsPerPage = 60.0;
int timeLeftMinutes = 0;
int autoSaveCounter = 0;

String currentBook = "";
uint32_t currentBookSize = 0;
#define MAX_PAGES 500
uint32_t pageStarts[MAX_PAGES];
int currentPage = 0;

// --- Prototypes ---
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

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  pinMode(36, OUTPUT); digitalWrite(36, LOW);
  pinMode(37, OUTPUT); digitalWrite(37, HIGH);
  delay(100);

  btn1.setClickMs(190);
  btn1.setPressMs(600);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(200000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.setTextColor(WHITE);
  display.setTextWrap(false);

  LittleFS.begin(true);
  prefs.begin("esbook", false);

  currentBrightness = prefs.getInt("bright", 4);
  isDarkMode = prefs.getBool("dark", true);
  currentBook = prefs.getString("lastBook", "");

  applySettings();
  drawAnimatedSplash();
  resetActivityTimer();

  btn1.attachClick([]() {
    resetActivityTimer();
    if (currentState == READING) changePage(1);
    else if (currentState == MENU_DELETE_CONFIRM) { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else if (currentState != WIFI_PORTAL) {
      menuCursorIndex++;
      if (menuCursorIndex >= numMenuItems) menuCursorIndex = 0;
      scrollX = 0; scrollComplete = false; scrollDelayStart = millis();
      drawMenu();
    }
  });

  btn1.attachDoubleClick([]() {
    resetActivityTimer();
    if (currentState == READING) changePage(-1);
    else if (currentState == WIFI_PORTAL) { stopWiFiPortal(); currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else if (currentState == MENU_PAUSE) { currentState = READING; pageStartTime = millis(); renderPage(); }
    else if (currentState == MENU_DELETE_CONFIRM) { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else if (currentState != MENU_MAIN) {
      menuCursorIndex--;
      if (menuCursorIndex < 0) menuCursorIndex = numMenuItems - 1;
      scrollX = 0; scrollComplete = false; scrollDelayStart = millis();
      drawMenu();
    }
  });

  btn1.attachLongPressStart([]() {
    resetActivityTimer();
    if (currentState == READING) {
      unsigned long duration = (millis() - pageStartTime) / 1000;
      if (duration > 5) avgSecondsPerPage = (avgSecondsPerPage * 0.7) + (duration * 0.3);
      uint32_t totalPagesEst = currentBookSize / 800;
      int pagesRemaining = totalPagesEst - currentPage;
      if (pagesRemaining < 0) pagesRemaining = 0;
      timeLeftMinutes = (pagesRemaining * avgSecondsPerPage) / 60;
      currentState = MENU_PAUSE; buildPauseMenu();
    }
    else if (currentState == MENU_DELETE_CONFIRM) { deleteBookAndCleanup(bookToDelete); currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else if (currentState != WIFI_PORTAL) executeMenuSelection();
  });

    if (currentBook != "" && currentBook != "none" && LittleFS.exists(currentBook)) { loadProgress(); currentState = READING; pageStartTime = millis(); renderPage(); }
    else { buildMainMenu(); }
}

void loop() {
  btn1.tick();

  if (currentState != READING && currentState != WIFI_PORTAL && numMenuItems > 0 && !scrollComplete) {
    String currentText = menuItems[menuCursorIndex];
    int limit = (currentState == MENU_MAIN) ? 95 : 110;
    String textToCheck = currentText;
    int prefixWidth = 0;
    if (currentText.startsWith("[+] ")) { textToCheck = currentText.substring(4); prefixWidth = 24; }
    int textPixelWidth = textToCheck.length() * 6;

    if (textPixelWidth > (limit - prefixWidth)) {
      if (millis() - scrollDelayStart > 1500) {
        if (millis() - lastScrollTime > 50) {
          scrollX++;
          if (scrollX >= (textPixelWidth + MARQUEE_GAP)) { scrollX = (textPixelWidth + MARQUEE_GAP); scrollComplete = true; }
          lastScrollTime = millis();
          drawMenu();
        }
      }
    }
  }

  if (currentState == WIFI_PORTAL) {
    server.handleClient();
    if (millis() - lastFooterTime > 40) {
      footerScrollX++;
      int tickerWidth = 28 * 6;
      if (footerScrollX >= (tickerWidth + MARQUEE_GAP)) footerScrollX = 0;
      lastFooterTime = millis();
      updateWiFiDisplay();
    }
    resetActivityTimer();
  }
  if (millis() - lastActivityTime > SLEEP_TIMEOUT) goToSleep();
}

void resetActivityTimer() { lastActivityTime = millis(); }

void deleteBookAndCleanup(String fullPath) {
  if (!fullPath.endsWith(".txt")) fullPath += ".txt";
  if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
  LittleFS.remove(fullPath);
  String posPath = fullPath; posPath.replace(".txt", ".pos");
  if (LittleFS.exists(posPath)) LittleFS.remove(posPath);
}

// --- Menus ---

void buildMainMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false; scrollDelayStart = millis();
  File root = LittleFS.open("/");
  File file = root.openNextFile();

  if (currentPath == "/") {
    while(file && numMenuItems < MAX_FILES - 2) {
      if (!file.isDirectory()) {
        String fn = String(file.name());
        if (fn.startsWith("/")) fn = fn.substring(1);
        if (fn.endsWith(".txt")) {
          int tildeIdx = fn.indexOf('~');
          if (tildeIdx > 0) {
            String authorName = fn.substring(0, tildeIdx);
            String virtualFolder = "[+] " + authorName;
            bool exists = false;
            for(int i = 0; i < numMenuItems; i++) {
              if(menuItems[i] == virtualFolder) { exists = true; break; }
            }
            if(!exists) menuItems[numMenuItems++] = virtualFolder;
          } else {
            menuItems[numMenuItems++] = fn.substring(0, fn.length() - 4);
          }
        }
      }
      file = root.openNextFile();
    }
    menuItems[numMenuItems++] = "-> Settings";
  } else {
    menuItems[numMenuItems++] = "[<-- Back]";
    String targetAuthor = currentPath.substring(1);
    while(file && numMenuItems < MAX_FILES) {
      if (!file.isDirectory()) {
        String fn = String(file.name());
        if (fn.startsWith("/")) fn = fn.substring(1);
        if (fn.endsWith(".txt") && fn.startsWith(targetAuthor + "~")) {
          String title = fn.substring(targetAuthor.length() + 1, fn.length() - 4);
          menuItems[numMenuItems++] = title;
        }
      }
      file = root.openNextFile();
    }
  }
  drawMenu();
}

void buildSettingsMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false;
  menuItems[numMenuItems++] = "Book Upload";
  menuItems[numMenuItems++] = "Display Setup";
  menuItems[numMenuItems++] = "Progress Reset";
  menuItems[numMenuItems++] = "Delete Book";
  menuItems[numMenuItems++] = "Power Off";
  menuItems[numMenuItems++] = "< Back to Books";
  drawMenu();
}

void buildDisplayMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false;
  menuItems[numMenuItems++] = "Brightness: " + String(currentBrightness);
  menuItems[numMenuItems++] = isDarkMode ? "Mode: DARK" : "Mode: LIGHT";
  menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void buildPauseMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false;
  menuItems[numMenuItems++] = "Continue Reading";
  menuItems[numMenuItems++] = "Save & Exit";
  drawMenu();
}

void buildDeleteMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false;
  File root = LittleFS.open("/");
  File entry = root.openNextFile();
  while(entry && numMenuItems < MAX_FILES - 1) {
    if (!entry.isDirectory()) {
      String fn = String(entry.name());
      if (fn.startsWith("/")) fn = fn.substring(1);
      if(fn.endsWith(".txt")) {
        String displayName = fn.substring(0, fn.length() - 4);
        displayName.replace("~", " - ");
        menuItems[numMenuItems++] = displayName;
      }
    }
    entry = root.openNextFile();
  }
  if (numMenuItems == 0) menuItems[numMenuItems++] = "Library empty";
  menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void buildResetMenu() {
  numMenuItems = 0; menuCursorIndex = 0; scrollX = 0; scrollComplete = false;
  File root = LittleFS.open("/"); File file = root.openNextFile();
  while(file && numMenuItems < MAX_FILES - 1){
    String fn = String(file.name());
    if (fn.startsWith("/")) fn = fn.substring(1);
    if(fn.endsWith(".pos")) {
      String displayName = fn.substring(0, fn.length() - 4);
      displayName.replace("~", " - ");
      menuItems[numMenuItems++] = displayName;
    }
    file = root.openNextFile();
  }
  if (numMenuItems == 0) menuItems[numMenuItems++] = "No progress found";
  menuItems[numMenuItems++] = "< Back";
  drawMenu();
}

void drawMenu() {
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  if (currentState == MENU_DELETE_CONFIRM) {
    display.setCursor(M_LEFT, M_TOP + 8); display.println("   !!! DELETE !!!");
    display.drawLine(M_LEFT, M_TOP + 18, M_RIGHT, M_TOP + 18, WHITE);
    display.setCursor(M_LEFT, M_TOP + 28); display.println("Permanently remove:");
    
    String displayBook = bookToDelete;
    displayBook.replace("~", " - ");
    if (displayBook.startsWith("/")) displayBook = displayBook.substring(1);
    
    display.setCursor(M_LEFT, M_TOP + 38); display.println(displayBook);
    display.setCursor(M_LEFT, M_BOTTOM - 8); display.println("Click:NO  Hold:YES");
    display.display(); return;
  }

  int startY, drawOffset;
  if (currentState == MENU_MAIN || currentState == MENU_PAUSE) {
    printStatusBar(currentState == MENU_PAUSE);
    display.setCursor(M_LEFT, M_TOP + 10);
    
    String headerPath = currentPath;
    headerPath.replace("~", " - ");
    
    if (currentState == MENU_MAIN) display.print(currentPath == "/" ? "--- Library ---" : "--- " + headerPath + " ---");
    else display.println("--- Paused ---");
    startY = M_TOP + 20; drawOffset = 3;
    if (currentState == MENU_PAUSE) { display.setCursor(M_LEFT + 10, M_BOTTOM - 8); display.print("Time Left: "); display.print(timeLeftMinutes); display.print("m"); }
  } else {
    display.setCursor(M_LEFT, M_TOP);
    if (currentState == MENU_SETTINGS) display.println("[ Settings ]");
    else if (currentState == MENU_DISPLAY) display.println("[ Display ]");
    else if (currentState == MENU_RESET_PROGRESS) display.println("[ Reset ]");
    else if (currentState == MENU_DELETE_SELECT) display.println("[ Delete Book ]");
    startY = M_TOP + 11; drawOffset = 4;
  }

  for (int i = 0; i < numMenuItems; i++) {
    int drawY = startY + (i * 10) - (menuCursorIndex > drawOffset ? (menuCursorIndex - drawOffset) * 10 : 0);
    if (drawY >= startY && drawY <= M_BOTTOM - 8) {
      String itemText = menuItems[i];
      if (i == menuCursorIndex) { display.setCursor(M_LEFT, drawY); display.print(">"); }
      if (itemText.startsWith("[+] ")) {
        display.setCursor(M_LEFT + 8, drawY); display.print("[+] ");
        String authorName = itemText.substring(4);
        int textWidth = authorName.length() * 6;
        if (i == menuCursorIndex && textWidth > (M_RIGHT - (M_LEFT + 32))) {
          display.setCursor(M_LEFT + 32 - scrollX, drawY); display.print(authorName);
          display.setCursor(M_LEFT + 32 - scrollX + textWidth + MARQUEE_GAP, drawY); display.print(authorName);
        } else { display.setCursor(M_LEFT + 32, drawY); display.print(authorName); }
      } else {
        int limit = (M_RIGHT - (M_LEFT + 8));
        int textWidth = itemText.length() * 6;
        if (i == menuCursorIndex && textWidth > limit) {
          display.setCursor(M_LEFT + 8 - scrollX, drawY); display.print(itemText);
          display.setCursor(M_LEFT + 8 - scrollX + textWidth + MARQUEE_GAP, drawY); display.print(itemText);
        } else { display.setCursor(M_LEFT + 8, drawY); display.print(itemText); }
      }
    }
  }
  display.display();
}

void executeMenuSelection() {
  String selected = menuItems[menuCursorIndex];
  if (currentState == MENU_MAIN) {
    if (selected == "[<-- Back]") { currentPath = "/"; buildMainMenu(); }
    else if (selected.startsWith("[+] ")) { 
      String folderName = selected.substring(4); 
      currentPath = "/" + folderName; 
      buildMainMenu(); 
    }
    else if (selected == "-> Settings") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else { 
      if (currentPath == "/") {
        currentBook = "/" + selected + ".txt"; 
      } else {
        String targetAuthor = currentPath.substring(1);
        currentBook = "/" + targetAuthor + "~" + selected + ".txt";
      }
      prefs.putString("lastBook", currentBook); loadProgress(); currentState = READING; pageStartTime = millis(); renderPage(); 
    }
  }
  else if (currentState == MENU_PAUSE) {
    if (selected == "Continue Reading") { currentState = READING; pageStartTime = millis(); renderPage(); }
    else { saveProgress(); currentBook = ""; prefs.putString("lastBook", "none"); currentState = MENU_MAIN; buildMainMenu(); }
  }
  else if (currentState == MENU_SETTINGS) {
    if (selected == "Book Upload") startWiFiPortal();
    else if (selected == "Display Setup") { currentState = MENU_DISPLAY; buildDisplayMenu(); }
    else if (selected == "Progress Reset") { currentState = MENU_RESET_PROGRESS; buildResetMenu(); }
    else if (selected == "Delete Book") { currentState = MENU_DELETE_SELECT; buildDeleteMenu(); }
    else if (selected == "Power Off") goToSleep();
    else if (selected == "< Back to Books") { currentState = MENU_MAIN; buildMainMenu(); }
  }
  else if (currentState == MENU_DISPLAY) {
    if (selected.indexOf("Mode") != -1) { isDarkMode = !isDarkMode; prefs.putBool("dark", isDarkMode); applySettings(); buildDisplayMenu(); }
    else if (selected.indexOf("Brightness") != -1) { currentBrightness++; if(currentBrightness > 4) currentBrightness = 1; prefs.putInt("bright", currentBrightness); applySettings(); buildDisplayMenu(); }
    else { currentState = MENU_SETTINGS; buildSettingsMenu(); }
  }
  else if (currentState == MENU_RESET_PROGRESS) {
    if (selected == "< Back" || selected == "No progress found") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else { 
      String target = selected; 
      target.replace(" - ", "~"); 
      target += ".pos"; 
      LittleFS.remove("/" + target); 
      buildResetMenu(); 
    }
  }
  else if (currentState == MENU_DELETE_SELECT) {
    if (selected == "< Back" || selected == "Library empty") { currentState = MENU_SETTINGS; buildSettingsMenu(); }
    else { 
      bookToDelete = "/" + selected; 
      bookToDelete.replace(" - ", "~");
      if (!bookToDelete.endsWith(".txt")) bookToDelete += ".txt"; 
      currentState = MENU_DELETE_CONFIRM; drawMenu(); 
    }
  }
}

// --- WiFi / Upload Portal ---

void startWiFiPortal() {
  currentState = WIFI_PORTAL; footerScrollX = 0;
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID);
  
  server.on("/", HTTP_GET, []() {
    String h = INDEX_HTML_TOP;
    File root = LittleFS.open("/"); 
    File file = root.openNextFile();
    String authors[MAX_FILES];
    int numAuthors = 0;
    
    while(file) { 
      if(!file.isDirectory()) { 
        String fn = String(file.name()); 
        if(fn.startsWith("/")) fn = fn.substring(1); 
        int tildeIdx = fn.indexOf('~');
        if (tildeIdx > 0) {
          String author = fn.substring(0, tildeIdx);
          bool exists = false;
          for (int i = 0; i < numAuthors; i++) {
            if (authors[i] == author) { exists = true; break; }
          }
          if (!exists && numAuthors < MAX_FILES) {
            authors[numAuthors++] = author;
            h += "<option value='" + author + "'>" + author + "</option>"; 
          }
        }
      } 
      file = root.openNextFile(); 
    }
    
    h += INDEX_HTML_BOTTOM;
    server.send(200, "text/html", h);
  });
  
  server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, []() {
    HTTPUpload& u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
      String author = server.arg("author_pick"); if (author == "") author = server.arg("author_new"); author.trim();
      String customTitle = server.arg("title"); customTitle.trim();
      
      String finalFileName = (customTitle == "") ? u.filename : customTitle;
      if (finalFileName.endsWith(".txt")) finalFileName = finalFileName.substring(0, finalFileName.length() - 4);
      
      if (author != "") {
        finalFileName = author + "~" + finalFileName + ".txt";
      } else {
        finalFileName = finalFileName + ".txt";
      }
      
      uploadFile = LittleFS.open("/" + finalFileName, "w");
    } else if (u.status == UPLOAD_FILE_WRITE && uploadFile) { uploadFile.write(u.buf, u.currentSize); }
    else if (u.status == UPLOAD_FILE_END && uploadFile) { uploadFile.close(); }
  });
  
  server.begin();
}

void updateWiFiDisplay() {
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(M_LEFT, M_TOP);      display.println("--- Upload Portal ---");
  display.drawLine(M_LEFT, M_TOP + 8, M_RIGHT, M_TOP + 8, WHITE);
  
  display.setCursor(M_LEFT, M_TOP + 18); display.print("SSID: "); display.println(apSSID);
  display.setCursor(M_LEFT, M_TOP + 30); display.print("IP:   "); display.println(apIP.toString());
  
  display.drawLine(M_LEFT, M_BOTTOM - 12, M_RIGHT, M_BOTTOM - 12, WHITE);
  String ticker = "Double-click to close portal";
  int tickerWidth = ticker.length() * 6;
  display.setCursor(M_LEFT - footerScrollX, M_BOTTOM - 8);
  display.print(ticker);
  display.setCursor(M_LEFT - footerScrollX + tickerWidth + MARQUEE_GAP, M_BOTTOM - 8);
  display.print(ticker);
  display.display();
}

void stopWiFiPortal() { server.stop(); WiFi.softAPdisconnect(true); }

void drawAnimatedSplash() {
  String v = "v" + String(random(0, 10)) + "." + String(random(0, 99));
  const char* suffixes[] = {"", "-beta", " [STABLE?]", " (Legacy)", ".rev6", "-rc1", " [Turbo]"};
  v += suffixes[random(0, 7)];
  for (int i = 0; i <= 100; i += 4) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2); display.setCursor(22, 18); display.print("EsBook32");
    display.drawRect(23, 45, 82, 8, WHITE);
    display.fillRect(25, 47, map(i, 0, 100, 0, 78), 4, WHITE);
    display.setTextSize(1); display.setCursor(64 - (v.length() * 3), 36); display.print(v);
    display.display(); delay(45);
  }
  delay(500);
}

void applySettings() {
  display.setRotation(0);
  uint8_t contrast = (currentBrightness == 1) ? 32 : (currentBrightness == 2) ? 96 : (currentBrightness == 3) ? 160 : 255;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
  if(isDarkMode) display.ssd1306_command(SSD1306_NORMALDISPLAY);
  else display.ssd1306_command(SSD1306_INVERTDISPLAY);
}

void printStatusBar(bool showProgress) {
  display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(M_LEFT, M_TOP); display.print("EsB32");
  if (showProgress && currentBookSize > 0) {
    int percent = (pageStarts[currentPage] * 100) / currentBookSize;
    display.setCursor(58, M_TOP); display.print(percent); display.print("%");
  }
  display.drawLine(M_LEFT, M_TOP + 8, M_RIGHT, M_TOP + 8, WHITE);
}

void loadProgress() {
  String p = currentBook; p.replace(".txt", ".pos");
  File f = LittleFS.open(p, "r");
  pageStarts[0] = f ? f.readString().toInt() : 0;
  currentPage = 0; if(f) f.close();
}

void saveProgress() {
  if (currentBook == "" || currentBook == "none") return;
  String p = currentBook; p.replace(".txt", ".pos");
  File f = LittleFS.open(p, "w");
  if(f) { f.print(pageStarts[currentPage]); f.close(); }
}

void changePage(int direction) {
  if (direction == 1 && pageStarts[currentPage + 1] > pageStarts[currentPage]) currentPage++;
  else if (direction == -1 && currentPage > 0) currentPage--;
  autoSaveCounter++;
  if (autoSaveCounter >= 10) { saveProgress(); autoSaveCounter = 0; }
  pageStartTime = millis(); renderPage();
}

void renderPage() {
  File f = LittleFS.open(currentBook, "r"); if (!f) return;
  currentBookSize = f.size();
  f.seek(pageStarts[currentPage]);
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  int x = M_LEFT, y = M_TOP;
  uint32_t lineStart = pageStarts[currentPage];
  pageStarts[currentPage+1] = 0;
  String word = "";
  while (f.available()) {
    char c = f.read();
    if (c != ' ' && c != '\n' && c != '\r') { word += c; }
    else {
      if (x + (word.length() * 6) > M_RIGHT) { x = M_LEFT; y += 8; if (y <= M_BOTTOM - 8) lineStart = f.position() - 1 - word.length(); }
      if (y > M_BOTTOM - 8) break;
      display.setCursor(x, y); display.print(word); x += (word.length() * 6);
      if (c == ' ') { if (x + 6 <= M_RIGHT) { display.print(" "); x += 6; } else { x = M_LEFT; y += 8; if (y <= M_BOTTOM - 8) lineStart = f.position(); } }
      else if (c == '\n') { x = M_LEFT; y += 8; if (y <= M_BOTTOM - 8) lineStart = f.position(); }
      if (y > M_BOTTOM - 8) break;
      word = "";
    }
  }
  display.display(); f.close(); pageStarts[currentPage + 1] = lineStart;
}

void goToSleep() {
  saveProgress();
  display.clearDisplay(); display.setCursor(20, 30); display.println("Sleeping"); display.display(); delay(1000);
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_1_PIN, 0);
  esp_deep_sleep_start();
}