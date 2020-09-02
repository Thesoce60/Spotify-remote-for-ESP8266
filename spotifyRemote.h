#ifndef SPOTIFY_REMOTE
#define SPOTIFY_REMOTE

#include <ArduinoJson.h>
#include "Arduino.h" //appelle de la librairie Arduino
#include <ESP8266WebServer.h>
#include <FS.h>
#include <base64.h>

typedef struct SpotPlayer {
  bool haveChange;
  bool isPlaying;
  String playing_type;
  long progress_millis;

  //track info
  String album_name;
  String artist_names[5];
  long track_duration;
  String track_name;
  String sml_track_name;
  //others
  String playerStateEvolution;
  int httpCode;//get the state(if a player is active)
} SpotPlayer;

class SpotifyRemote {
  public:
    SpotifyRemote(String clientId, String clientSecret);
    bool Connect(String ssid, String password);
    bool setup();
    void getToken(String authCode, bool isFirst);
    int play();
    int pause();
    int playerCommand(String method, String command);
    SpotPlayer updatePlayerState();

    const String FROM_PLAY = "from_play";
    const String FROM_PAUSE = "from_pause";
    const int NO_PLAYER_HTTPCODE = 204;
    const int NOT_VALID_TOKEN_HTTPCODE = 401;
    const int NOT_PREMIUM_HTTPCODE = 403;
  private:
    SpotPlayer player;
    String _clientId, _clientSecret;
    String refresh_token, access_token;
    const int httpsPort = 443;
    String loadRefreshToken();
    void saveRefreshToken(String refreshToken);
    String startConfigPortal();
    String redirectUri = "http://esp8266.local/callback/";
    ESP8266WebServer server;
    bool isDataCall;
    void jsonParseRToken(WiFiClientSecure client);
    void parsePlayerState(WiFiClientSecure client);
    void serializePlayer();
    StaticJsonDocument<8000> jsonBuffer;

};

#endif
