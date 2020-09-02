#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>



#include "spotifyRemote.h"
SpotifyRemote::SpotifyRemote(String clientId, String clientSecret) {
  _clientId = clientId;
  _clientSecret = clientSecret;
}
bool SpotifyRemote::setup() {
  if (!MDNS.begin("esp8266")) {             // Start the mDNS responder for esp8266.local
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  MDNS.addService("http", "tcp", 80);
  SPIFFS.begin();
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  Serial.printf("SPIFFS: %lu of %lu bytes used.\n", fs_info.usedBytes, fs_info.totalBytes);

  if (loadRefreshToken() == "") {
    Serial.println("No refresh token found. Requesting through browser");
    //    Serial.println ( "Open browser at http://" + espotifierNodeName + ".local" );
    getToken(startConfigPortal(), true);
  } else {
    Serial.println("Using refresh token found on the FS");
    getToken(loadRefreshToken(), false);
  }
  Serial.printf("Refresh token: %s\nAccess Token: %s\n", refresh_token.c_str(), access_token.c_str());
  if (refresh_token != "") {
    saveRefreshToken(refresh_token);
    return true;
  }
  return false;
}
bool SpotifyRemote::Connect(String ssid, String password) {
  Serial.println();
  Serial.print("connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (retries > 20)
      return false;
    retries++;
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  return 1;
}

void SpotifyRemote::getToken(String authCode, bool isFirst) {
  WiFiClientSecure client;
  client.setInsecure();
  Serial.println("connection to the auth server");
  const char* host = "accounts.spotify.com";
  const int port = 443;
  String url = "/api/token";
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  //Serial.print("Requesting URL: ");
  String codeParam = "code";
  String grantType = "authorization_code";
  if (!isFirst) {
    codeParam = "refresh_token";
    grantType = "refresh_token";
  }
  String authorizationRaw = _clientId + ":" + _clientSecret;
  String authorization = base64::encode(authorizationRaw, false);
  // This will send the request to the server
  String content = "grant_type=" + grantType + "&" + codeParam + "=" + authCode + "&redirect_uri=" + redirectUri;
  client.print("POST " + url + " HTTP/1.1\r\n");
  client.print("Host: " + String(host) + "\r\n");
  client.print("Authorization: Basic " + authorization + "\r\n");
  client.print("Content-Length: " + String(content.length()) + "\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(content);
  int retryCounter = 0;
  Serial.print("Waiting a " + String(host) + " response");
  bool Response = 0;
  int httpCode;
  while (!Response) {
    delay(50);
    int len = client.available();
    Serial.println(len);
    if (len > 0) {
      Serial.println("Get token : spotResponse");
      boolean isBody = false;
      client.setNoDelay(false);
      Serial.println("Get token : Waiting response....");
      while (client.connected() || client.available()) {
        while ((client.available()) > 0) {
          if (isBody) {
            if (httpCode == 200) {
              jsonParseRToken(client);
              Response = true;
              if (!isFirst) {
                refresh_token = loadRefreshToken();
              }
            }
            return ;
          } else {
            String line = client.readStringUntil('\r');
            //Serial.println(line);
            if (line.startsWith("HTTP/1.")) {
              httpCode = line.substring(9, line.indexOf(' ', 9)).toInt();
              Serial.printf("HTTP Code: %d\n", httpCode);
              player.httpCode = httpCode;
            }
            if (line == "\r" || line == "\n" || line == "") {
              //Serial.println("Body starts now");
              isBody = true;
            }
          }
        }
      }
    }
  }
}

SpotPlayer SpotifyRemote::updatePlayerState() {
  WiFiClientSecure client;
  //Serial.println("Update called");
  client.setInsecure();
  String host = "api.spotify.com";
  const int port = 443;
  String url = "/v1/me/player/currently-playing";
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    player.haveChange = false;
    return player;
  }
  //Serial.print("Requesting URL: ");
  //Serial.println(url);
  String request = "GET " + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Authorization: Bearer " + access_token + "\r\n" +
                   "Content-Length: 0\r\n" +
                   "Connection: close\r\n\r\n";
  //Serial.println(request);
  client.print(request);
  bool Response = false;
  int retries = 0;
  boolean isBody = false;
  client.setNoDelay(false);
  while (!Response) {
    delay(50);
    int len = client.available();
    Serial.print('.');
    if (retries > 30) {
      player.haveChange = false;
      return player;
    }
    retries++;
    if (len > 0) {
      boolean isBody = false;
      client.setNoDelay(false);
      uint16_t httpCode = 0;
      Serial.println("available : " + String(client.available()) + "o");
      while (client.connected() || client.available()) {
        while ((client.available()) > 0) {
          if (isBody) {
            if (httpCode == 200) {
              parsePlayerState(client);
              player.haveChange = true;
              return player;
            }
            if (httpCode == NOT_VALID_TOKEN_HTTPCODE) {
              getToken(loadRefreshToken(), false);
              Serial.printf("Refresh token: %s\nAccess Token: %s\n", refresh_token.c_str(), access_token.c_str());
              if (refresh_token != "") {
                saveRefreshToken(refresh_token);
              }
            }
            player.haveChange = false;
            return player;
          } else {
            String line = client.readStringUntil('\r');
            //Serial.println(line);
            if (line.startsWith("HTTP/1.")) {
              httpCode = line.substring(9, line.indexOf(' ', 9)).toInt();
              Serial.printf("HTTP Code: %d\n", httpCode);
              player.httpCode = httpCode;
            }
            if (line == "\r" || line == "\n" || line == "") {
              //Serial.println("Body starts now");
              isBody = true;
            }
          }
        }
      }
    }
  }
}

void SpotifyRemote::parsePlayerState(WiFiClientSecure client) {
  Serial.println(ESP.getFreeHeap());
  jsonBuffer.clear();
  auto error = deserializeJson(jsonBuffer, client);
  if (error) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
    return;
  }
  //  playerStateEvolution
  bool play_state = jsonBuffer["is_playing"].as<bool>();
  if (player.isPlaying != play_state) {
    player.playerStateEvolution = "from_" + (player.isPlaying ? String("play") : String("pause"));
  } else {
    player.playerStateEvolution = "no";
  }
  player.isPlaying = play_state;
  player.playing_type = jsonBuffer["currently_playing_type"].as<String>();
  player.progress_millis = jsonBuffer["progress_ms"].as<long>();
  JsonObject item = jsonBuffer["item"];
  player.album_name = item["album"]["name"].as<String>();
  JsonArray artists = item["artists"];
  for (byte i = 0; i < 5; i++) {
    player.artist_names[i] = "";
  }
  int counter = 0;
  for (JsonObject elem : artists) {
    player.artist_names[counter] = elem["name"].as<String>();
    counter++;
  }
  player.track_duration = item["duration_ms"].as<long>();
  player.track_name = item["name"].as<String>();
  //serializePlayer();
  jsonBuffer.clear();
  int ind = player.track_name.indexOf('-');
  if (ind != -1 && player.track_name.length() > 25) {
    player.sml_track_name = player.track_name.substring(0, ind) + "...";
  } else {
    player.sml_track_name = player.track_name;
  }
  if (player.playing_type == "ad") {
    player.track_duration = 30000;
    player.track_name = "Spotify ad";
    player.artist_names[0] = "Spotify";
  }
}
void SpotifyRemote::serializePlayer() {
  Serial.println("-------------------------------------------------------------------------");
  String buf = "State :" + (player.isPlaying ? String("play") : String("pause"));
  Serial.println(buf);
  Serial.println("Type :" + player.playing_type);
  Serial.println("Song :" + player.track_name);
  int percent = map(player.progress_millis, 0, player.track_duration, 0, 100);
  Serial.println("Song :[" + String(percent) + "%]");
  Serial.println("Song :" + String(player.progress_millis) + '/' + String(player.track_duration));
  Serial.println("Album :" + player.album_name);
  Serial.println("Evolution :" + player.playerStateEvolution);
  buf = "";
  for (String art : player.artist_names) {
    if (art == "")
      break;
    buf = buf + art + ',';
  }
  Serial.println("Artist :" + buf);
}



void SpotifyRemote::jsonParseRToken(WiFiClientSecure client) {
  jsonBuffer.clear();
  auto error = deserializeJson(jsonBuffer, client);
  if (error) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
    return;
  }
  refresh_token = jsonBuffer["refresh_token"].as<String>();
  access_token = jsonBuffer["access_token"].as<String>();
}





String SpotifyRemote::loadRefreshToken() {
  Serial.println("Loading config");
  File f = SPIFFS.open("/refreshToken.txt", "r");
  if (!f) {
    Serial.println("Failed to open config file");
    return "";
  }
  while (f.available()) {
    //Lets read line by line from the file
    String token = f.readStringUntil('\r');
    Serial.printf("Refresh Token: %s\n", token.c_str());
    f.close();
    if (token.length() < 10)
      return "";
    return token;
  }
  return "";
}
int SpotifyRemote::play() {
  return playerCommand("PUT", "play");
}
int SpotifyRemote::pause() {
  return playerCommand("PUT", "pause");
}

void SpotifyRemote::saveRefreshToken(String refreshToken) {
  Serial.println("Saving refreshToken");
  File f = SPIFFS.open("/refreshToken.txt", "w+");
  if (!f) {
    Serial.println("Failed to open config file");
    return;
  }
  f.println(refreshToken);
  f.close();
}

int SpotifyRemote::playerCommand(String method, String command) {
  Serial.print("Requesting command: " + command);
  WiFiClientSecure client;
  client.setInsecure();
  String host = "api.spotify.com";
  const int port = 443;
  String url = "/v1/me/player/" + command;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return -1;
  }
  Serial.print("Requesting URL: ");
  //Serial.println(url);
  String request = method + " " + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Authorization: Bearer " + access_token + "\r\n" +
                   "Content-Length: 0\r\n" +
                   "Connection: close\r\n\r\n";
  //Serial.println(request);
  client.print(request);
  bool Response = false;
  int retries = 0;
  while (!Response) {
    delay(50);
    int len = client.available();
    Serial.print('.');
    if (retries > 20)
      return -1;
    retries++;
    if (len > 0) {
      int bufLen = 1024;
      unsigned char buf[bufLen];
      boolean isBody = false;
      char c = ' ';

      int size = 0;
      client.setNoDelay(false);
      uint16_t httpCode = 0;
      while (client.connected() || client.available()) {
        while ((size = client.available()) > 0) {
          if (isBody) {
            uint16_t len = min(bufLen, size);
            c = client.readBytes(buf, len);
            for (uint16_t i = 0; i < len; i++) {
              Serial.print((char)buf[i]);
            }
          } else {
            String line = client.readStringUntil('\r');
            Serial.println(line);
            if (line.startsWith("HTTP/1.")) {
              httpCode = line.substring(9, line.indexOf(' ', 9)).toInt();
              Serial.printf("HTTP Code: %d\n", httpCode);
              return httpCode;
            }
            if (line == "\r" || line == "\n" || line == "") {
              Serial.println("Body starts now");
              isBody = true;
            }
          }
        }
      }
    }
  }
}










String SpotifyRemote::startConfigPortal() {
  String oneWayCode = "";

  server.on ( "/", [this]() {
    Serial.println(_clientId);
    server.sendHeader("Location", String("https://accounts.spotify.com/authorize/?client_id="
                                         + _clientId
                                         + "&response_type=code&redirect_uri=http://esp8266.local/callback/"
                                         + "&scope=user-read-private%20user-read-currently-playing%20user-read-playback-state%20user-modify-playback-state"), true);
    server.send ( 302, "text/plain", "");
  } );

  server.on ( "/callback/", [this, &oneWayCode]() {
    if (!server.hasArg("code")) {
      server.send(500, "text/plain", "BAD ARGS");
      return;
    }

    oneWayCode = server.arg("code");
    Serial.printf("Code: %s\n", oneWayCode.c_str());

    String message = "<html><head></head><body>Succesfully authentiated This device with Spotify. Restart your device now</body></html>";

    server.send ( 200, "text/html", message );
  } );

  server.begin();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
  } else {
    Serial.println("WiFi not connected!");
  }

  Serial.println ( "HTTP server started" );

  while (oneWayCode == "") {
    server.handleClient();
    MDNS.update();
    yield();
  }
  server.stop();
  return oneWayCode;
}
