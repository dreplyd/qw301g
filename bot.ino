#include <stack>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <Adafruit_HTU21DF.h>
#include <ArduinoJson.h>


const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char* BOT_TOKEN = "";

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

unsigned long last_time_bot_accessed = 0;
unsigned long previous_millis = 0;
unsigned int readings_count = 0;
const unsigned long bot_request_delay = 1000;
const unsigned long request_time_interval = 5 * 60 * 1000;
const unsigned long readings_interval = 1 * 60 * 1000;
const int max_readings_count = request_time_interval / readings_interval;
const char* main_menu_keyboard = "[[\"Поточні показники\", \"Звіт за тиждень\"]]";
const char* report_menu_keyboard = "[[\"Температура\", \"Вологість\"], [\"Назад\"]]";
String server_api = "";

enum MenuState {
  MAIN_MENU,
  REPORT_MENU,
};
std::stack<MenuState> menu_state;

struct SensorData {
  float temperature;
  float humidity;
  uint32_t timestamp;
};
SensorData sensor_data[max_readings_count];


uint32_t getCurrentTime() {
  time_t now = time(nullptr);

  return static_cast<uint32_t>(now);
}


String getKeyboardByMenu() {
  switch (menu_state.top()) {
    case MAIN_MENU:
      return main_menu_keyboard;
    case REPORT_MENU:
      return report_menu_keyboard;
  }
}


void sendDataToServer() {
  HTTPClient http;
  http.begin(server_api + "/save-data");
  http.addHeader("Content-Type", "application/json");

  const size_t capacity = JSON_ARRAY_SIZE(max_readings_count) + max_readings_count * JSON_OBJECT_SIZE(3);
  StaticJsonDocument<capacity> doc;

  JsonArray json_array = doc.to<JsonArray>();
  for (int i = 0; i < max_readings_count; i++) {
    JsonObject json_object = json_array.createNestedObject();
    json_object["temperature"] = sensor_data[i].temperature;
    json_object["humidity"] = sensor_data[i].humidity;
    json_object["timestamp"] = sensor_data[i].timestamp;
  }

  String json_string;
  serializeJson(doc, json_string);

  int http_status_code = http.POST(json_string);

  if (http_status_code == HTTP_CODE_OK) {
    readings_count = 0;
  }

  http.end();
}


void getPlotByType(String chat_id, String type) {
  String reply_text;

  HTTPClient http;
  http.begin(server_api + "/generate-plot/" + type + "/" + chat_id);

  int http_status_code = http.GET();

  if (http_status_code == HTTP_CODE_OK) {
    String response = http.getString();

    StaticJsonDocument<64> doc;
    deserializeJson(doc, response);

    const char* plot_name = doc["plot"];
    String plot_url = server_api + "/static/" + plot_name + "?timestatmp=" + String(millis());

    reply_text = "Ось створений графік на основі даних за останній тиждень 📈:\n";
    bot.sendMessage(chat_id, reply_text);
    bot.sendPhoto(chat_id, plot_url);

    reply_text = "Чим я ще можу бути корисним?";
    bot.sendMessage(chat_id, reply_text);
  } else {
    reply_text = "Мені не вдалося створити графік 😔\n";
    reply_text += "Оберіть, будь ласка, інший звіт:";
    bot.sendMessage(chat_id, reply_text);
  }

  http.end();
}


void sendFallbackMessage(String chat_id) {
  String reply_text = "Я не зрозумів ваш запит 😔\n";
  reply_text += "Оберіть, будь ласка, один з пунктів меню нижче:";
  bot.sendMessageWithReplyKeyboard(chat_id, reply_text, "", getKeyboardByMenu(), true);
}


void handleReportMenu(String chat_id, String text) {
  String reply_text;

  if (text == "Температура") {
    getPlotByType(chat_id, "temperature");
  } else if (text == "Вологість") {
    getPlotByType(chat_id, "humidity");
  } else {
    sendFallbackMessage(chat_id);
  }
}


void handleMainMenu(String chat_id, String text) {
  String reply_text;
  
  if (text == "Поточні показники") {
    float temperature = round(htu.readTemperature() * 10) / 10;
    float humidity = round(htu.readHumidity() * 10) / 10;

    reply_text = "Ось поточні показники в приміщені:\n";
    reply_text += "🌡 Температура: " + String(temperature) + "°C\n";
    reply_text += "💧 Вологість: " + String(humidity) + "%";
    bot.sendMessage(chat_id, reply_text);

    reply_text = "Як я можу допомогти вам ще?";
    bot.sendMessage(chat_id, reply_text);
  } else if (text == "Звіт за тиждень") {
    menu_state.push(REPORT_MENU);

    reply_text = "Звіт формується на основі записів за останній тиждень.\n";
    reply_text += "Будь ласка, оберіть, який звіт ви бажаєте переглянути:";
    bot.sendMessageWithReplyKeyboard(chat_id, reply_text, "", report_menu_keyboard, true);
  } else {
    sendFallbackMessage(chat_id);
  }
}


void sendResponseMenu(String chat_id, MenuState menu) {
  String reply_text;

  switch (menu) {
    case MAIN_MENU:
      reply_text = "Як я можу допомогти вам ще?";
      bot.sendMessageWithReplyKeyboard(chat_id, reply_text, "", main_menu_keyboard, true);
      break;
  }
}


void handleBackCommand(String chat_id) {
  String reply_text;

  if (menu_state.top() == MAIN_MENU) {
    reply_text = "Як я можу допомогти вам ще?";
    bot.sendMessage(chat_id, reply_text);
  } else {
    menu_state.pop();
    sendResponseMenu(chat_id, menu_state.top());
  }
}


void handleStartCommand(String chat_id) {
  String reply_text;

  if (menu_state.empty()) {
    menu_state.push(MAIN_MENU);

    reply_text = "Привіт! Я ваш помічник для контролю мікроклімату 🏠🌡\n";
    reply_text += "Чим я можу ваш допомогти?";
    bot.sendMessageWithReplyKeyboard(chat_id, reply_text, "", main_menu_keyboard, true);
  } else {
    while (!menu_state.empty()) {
      menu_state.pop();
    }

    menu_state.push(MAIN_MENU);

    reply_text = "Хм, я був впевнений, що ми вже розмовляли 🤔\n";
    reply_text += "Чим я можу допомогти?";
    bot.sendMessageWithReplyKeyboard(chat_id, reply_text, "", main_menu_keyboard, true);
  }
}


void handleNewMessages(int num_new_messages) {
  for (int i = 0; i < num_new_messages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    if (text == "/start") {
      handleStartCommand(chat_id);
      break;
    } else if (text == "Назад") {
      handleBackCommand(chat_id);
      break;
    }

    switch (menu_state.top()) {
      case MAIN_MENU:
        handleMainMenu(chat_id, text);
        break;
      case REPORT_MENU:
        handleReportMenu(chat_id, text);
        break;
    }
  }
}


void setup() {
  Serial.begin(115200);

  Serial.println("Connecting to Wi-Fi.");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  configTime(0, 0, "pool.ntp.org");

  if (!htu.begin()) {
    while(1);
  }
}


void loop() {
  unsigned long current_millis = millis();

  if (current_millis > last_time_bot_accessed + bot_request_delay) {
    int num_new_messages = bot.getUpdates(bot.last_message_received + 1);

    while (num_new_messages) {
      handleNewMessages(num_new_messages);
      num_new_messages = bot.getUpdates(bot.last_message_received + 1);
    }

    last_time_bot_accessed = current_millis;
  }

  if (current_millis - previous_millis >= readings_interval) {
    previous_millis = current_millis;

    float temperature = round(htu.readTemperature() * 10) / 10;
    float humidity = round(htu.readHumidity() * 10) / 10;
    uint32_t timestamp = getCurrentTime();

    sensor_data[readings_count].temperature = temperature;
    sensor_data[readings_count].humidity = humidity;
    sensor_data[readings_count].timestamp = timestamp;

    readings_count++;

    if (readings_count >= max_readings_count) {
      sendDataToServer();
    }
  }
}
