#include <LiquidCrystal.h>

// 按鍵接腳
const int BTN_PIN = 2;
// 可變電阻接腳
const int POT_PIN = A0;
// LCD接腳 
const int LCD_RS = 12;
const int LCD_EN = 11;
const int LCD_D4 = 5;
const int LCD_D5 = 4;
const int LCD_D6 = 3;
const int LCD_D7 = 7;

// 防彈跳延遲
const int DEBOUNCE_DELAY = 50;
// 雙擊判定間隔
const int DOUBLE_CLICK_GAP = 250;

// 初始化 LCD 物件
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// --- 全域邏輯變數 ---
const String CHAR_MAP = " ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // 候選字表
int charIndex = 0;   // 目前選到的字元索引
int cursorCol = 0;   // 目前 LCD X 座標 (0-15)
int cursorRow = 0;   // 目前 LCD Y 座標 (0-1)

// Buffer
char textBuffer[2][17] = {
  "                ",
  "                " 
};

// --- 可變電阻邏輯 ---
// 安全鎖定旗標 (Soft Takeover Flag)
// false: 控制停用
// true:  控制啟用
bool potLocked = false;
int lastPotPos = -1;

// --- 按鍵狀態 ---
unsigned long lastDebounceTime = 0; // 上次訊號改變的時間點
unsigned long pressStartTime = 0;   // 按下按鈕的時間點
bool buttonState = HIGH;            // 目前穩定的按鍵狀態
bool lastButtonState = HIGH;        // 上一次讀取的原始狀態
bool waitingForDoubleClick = false; // 正在等待雙擊的第二下
unsigned long singleClickTimer = 0; // 單擊確認計時器

// --- 函式宣告 ---
void restorePosition(int col, int row);
void showPreview();
void clearBuffer();
void handlePotentiometer();
void handleDoubleClick();
void handleLongPressClear();
void handleHoldComma();
void handleSingleClick();

void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(POT_PIN, INPUT);
  
  // 初始化 LCD
  lcd.begin(16, 2);
  
  cursorCol = 0;
  cursorRow = 0;
  charIndex = 0;      
  potLocked = false;
  lastPotPos = -1;    
  
  showPreview(); // 顯示初始游標
}

void loop() {
  // 處理可變電阻移動
  handlePotentiometer();

  // 讀取按鍵原始訊號
  int reading = digitalRead(BTN_PIN);
  
  // --- 防彈跳 ---
  // 如果按下重置計時器
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  // 當穩定超過 DEBOUNCE_DELAY 視為狀態改變
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    
    // 如果穩定的狀態跟目前紀錄的狀態不同，代表真的發生了按壓或放開
    if (reading != buttonState) {
      buttonState = reading;
      
      // --- 狀態：按下 (Falling Edge) ---
      if (buttonState == LOW) { 
        if (waitingForDoubleClick) {
          // 如果在等待期間又按了一下 -> 觸發雙擊
          handleDoubleClick();
          waitingForDoubleClick = false; // 清除等待旗標
        } else {
          // 這是新的按壓，記錄開始時間
          pressStartTime = millis();
        }
      } 
      // --- 狀態：放開 (Rising Edge) ---
      else { 
        unsigned long duration = millis() - pressStartTime;
        
        // 只有在非雙擊等待模式下，才去判斷長短按
        if (!waitingForDoubleClick) {
          if (duration >= 1000) {
             // 按住超過 1秒 -> 清除
             handleLongPressClear();
          } else if (duration >= 500) {
             // 按住超過 0.5秒 -> 逗號
             handleHoldComma();
          } else {
             // 短按 (<0.5秒) -> 可能是單擊，但也可能是雙擊的第一下
             // 所以這裡不執行動作，而是進入「等待雙擊」模式
             waitingForDoubleClick = true;
             singleClickTimer = millis();
          }
        }
      }
    }
  }
  
  // 更新上一次的讀值供下一輪迴圈比對
  lastButtonState = reading;
  
  // --- 3. 雙擊超時判定 (Timeout Check) ---
  // 如果正在等待雙擊，且時間超過 250ms 都沒有第二下 -> 確認為單擊
  if (waitingForDoubleClick && (millis() - singleClickTimer > DOUBLE_CLICK_GAP)) {
    handleSingleClick();
    waitingForDoubleClick = false; // 結束等待
  }
}

// --- 功能實作區 (Action Handlers) ---

void handlePotentiometer() {
  int rawValue = analogRead(POT_PIN);
  // 將 0-1023 對應到 LCD 的 32 個格子 (0-31)
  int targetLinearPos = map(rawValue, 0, 1023, 0, 31);
  int currentLinearPos = cursorRow * 16 + cursorCol;

  // 停用可變電阻控制游標，直到轉動至目前游標位置。
  if (!potLocked) {
    if (abs(targetLinearPos - currentLinearPos) <= 1) {
      potLocked = true;
    }
    return;
  }

  // --- 游標移動處理 ---
  if (targetLinearPos != currentLinearPos) {
    if (targetLinearPos != lastPotPos) {
      
      // 把舊位置原本的字列印回去
      restorePosition(cursorCol, cursorRow); 
      
      // 計算新座標
      cursorRow = targetLinearPos / 16;
      cursorCol = targetLinearPos % 16;
      lastPotPos = targetLinearPos;
      
      // 在新位置顯示候選字
      showPreview(); 
    }
  }
}

void handleSingleClick() {
  
  // 切換到下一個字元
  charIndex++;
  if (charIndex >= CHAR_MAP.length()) {
    charIndex = 0; // 循環回到開頭
  }
  
  showPreview();
}

void handleHoldComma() {
  
  // 將逗號寫入 Buffer
  textBuffer[cursorRow][cursorCol] = ',';
  
  // 列印逗號
  lcd.setCursor(cursorCol, cursorRow);
  lcd.print(',');
  lcd.setCursor(cursorCol, cursorRow);
  lcd.blink(); // 保持閃爍

  // 游標右移
  cursorCol++;
  if (cursorCol > 15) {
    cursorCol = 0;
    cursorRow = (cursorRow + 1) % 2; 
  }
  
  showPreview();
}

void handleDoubleClick() {
  
  // 確認並儲存當前字元
  char confirmedChar = CHAR_MAP[charIndex];
  textBuffer[cursorRow][cursorCol] = confirmedChar;
  
  // 列印在 LCD 上
  lcd.setCursor(cursorCol, cursorRow);
  lcd.print(confirmedChar);
  
  // 游標移動到下一格
  cursorCol++;
  if (cursorCol > 15) {
    cursorCol = 0;
    cursorRow = (cursorRow + 1) % 2; 
  }
  potLocked = false;
  
  // 將 index 設為 -1，讓下一次單擊邏輯正常重置為 0
  charIndex = -1; 
  
  showPreview();
}

void handleLongPressClear() {
  
  clearBuffer();
  lcd.clear();
  
  // 全部重置回初始狀態
  cursorCol = 0;
  cursorRow = 0;
  charIndex = 0;
  potLocked = false;
  lastPotPos = -1;
  
  showPreview(); 
}


// 將 Buffer 全部填入空白
void clearBuffer() {
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 16; col++) {
      textBuffer[row][col] = ' ';
    }
    textBuffer[row][16] = '\0';
  }
}

// 把 Buffer 裡原本存的字元重新列印
void restorePosition(int col, int row) {
  lcd.setCursor(col, row);
  lcd.print(textBuffer[row][col]);
}

// 顯示目前的候選字 (預覽模式)
void showPreview() {
  lcd.setCursor(cursorCol, cursorRow);
  
  if(charIndex == -1) lcd.print(CHAR_MAP[charIndex+1]); 
  else lcd.print(CHAR_MAP[charIndex]); 
  
  lcd.setCursor(cursorCol, cursorRow);
  lcd.blink();
}

