#ifndef webpage_h
#define webpage_h

#include "FS.h"

extern ESP8266WebServer server;

//Opens file and send it to browser
void sendbestand(const char* path,const char* content){
  
  if (!SPIFFS.exists(path)){
     #ifdef DEBUG
        Serial.println("File not found " + String(path));
     #endif
     server.send ( 400, "text/html", "Page not Found" );
     return;
  }
  
  File file = SPIFFS.open( path, "r" );
  server.streamFile( file, content );
  file.close();
  #ifdef DEBUG
    Serial.println("Send " + String(path));
  #endif
}

//Concatinate char arrays and items
class Stringbuffer {
     size_t BUFFER_SIZE;
     char* buff;
  public:
    Stringbuffer (size_t size){BUFFER_SIZE = size; buff = new char[size]; buff[0] = '\0';buff[size-1] = '\0';}
    ~Stringbuffer(){delete[] buff;}
    void add(const char* array){ strncat(buff, array, BUFFER_SIZE - strlen(buff) - 1); }
    void add(int i){ char buffer [12];itoa (i,buffer,10);strncat(buff, buffer, BUFFER_SIZE - strlen(buff) - 1); }
    void add(unsigned long i){char buffer [11];ultoa(i,buffer,10);strncat(buff, buffer, BUFFER_SIZE - strlen(buff) - 1); }
    void addTime (int hr, int min, double sec){ if( hr > 99 && min > 99 && sec > 99.0){return;}
                                                char timebuff [9] = ""; char numberbuff [9] = "";
                                                itoa (hr,numberbuff,10);strcat(timebuff, numberbuff);
                                                strcat(timebuff, ":");
                                                itoa (min,numberbuff,10);if(numberbuff[1]=='\0'){strcat(timebuff, "0");};strcat(timebuff, numberbuff);
                                                strcat(timebuff, ":");
                                                int isec=(int)(sec+0.5);itoa (isec,numberbuff,10);if(numberbuff[1]=='\0'){strcat(timebuff, "0");};strcat(timebuff, numberbuff);
                                                strncat(buff, timebuff, BUFFER_SIZE - strlen(buff) - 1);
                                               }
    void add(double i){String cast = String(i);strncat(buff, cast.c_str(), BUFFER_SIZE - strlen(buff) - 1);}
    void addColor(RgbColor c){ String i="#";
                               i+=String(c.R>>4,HEX);i+=String(c.R & 0xf,HEX);
                               i+=String(c.G>>4,HEX);i+=String(c.G & 0xf,HEX);
                               i+=String(c.B>>4,HEX);i+=String(c.B & 0xf,HEX);
                               strncat(buff, i.c_str(),BUFFER_SIZE - strlen(buff) - 1);
                             }

    char* getPointer(){return buff;}
};

void closeAllConnections(){
   server.close();
   webSocket.disconnect();
}

/////////////////////////////////
//        Websocket            //
/////////////////////////////////

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {

    switch(type) {
        case WStype_DISCONNECTED:
            #ifdef DEBUG
                Serial.printf("[%u] Disconnected!\n", num);
            #endif
            break;
            
        case WStype_CONNECTED:
            #ifdef DEBUG
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                if (dataError){
                    webSocket.disconnect();
                }
            #endif
            break;
            
    }

}

void webSocketSendData(){
    
    double unix = (sat.satJd - 2440587.5)*86400000.0;   //unix in millis 

    uint8_t buffer[sizeof(double)*7+sizeof(int16_t)];
    
    memcpy(&buffer[0],&sat.satLon,sizeof(double));
    memcpy(&buffer[sizeof(double)],&sat.satLat,sizeof(double));
    memcpy(&buffer[sizeof(double)*2],&sat.satAlt,sizeof(double));
    memcpy(&buffer[sizeof(double)*3],&sat.satAz,sizeof(double));
    memcpy(&buffer[sizeof(double)*4],&sat.satEl,sizeof(double));
    memcpy(&buffer[sizeof(double)*5],&sat.satDist,sizeof(double));
    memcpy(&buffer[sizeof(double)*6],&sat.satVis,sizeof(int16_t));
    memcpy(&buffer[sizeof(double)*6+sizeof(int16_t)],&unix,sizeof(double));

    webSocket.broadcastBIN(buffer, sizeof(buffer));
}

/////////////////////////////////
//        Data handlers        //
/////////////////////////////////

void senddata(){

    if (!dataError){
        int year,mon,day,hr,min;
        double sec;
    
        bool calc=false;
        bool err=true;
        
        passinfo* Predictions;
        
        Stringbuffer buf( 12+86*pred_size + 2*(80+12) + (12+18) + (9+25) + (2*23));
    
        if (server.args() > 0 && predError){   ///check for extra arguments
            
            char *ptr;
            uint32_t unix = strtoul(server.arg(0).c_str(),&ptr,10);  ///read unix time
            double jdC = getJulianFromUnix(unix);
      
            ///recalc new predictions
            LedStrip.AnimStart(ANIM_WAIT);
            
            calc = true;
            Predictions = new passinfo[pred_size];
            if (server.argName(0) == "pre"){
                err = predictPasses(Predictions,jdC, true);
            }else if (server.argName(0) == "next"){
                err = predictPasses(Predictions,jdC, false);
            }else{
                err = false;
            }
    
            LedStrip.AnimStop();
            
        }else{
          Predictions = passPredictions;  /// normal prediction
        }
        
        if (predError && err){
            buf.add("tab|pass|8|");
            buf.add(pred_size);
            
            for (int i = 0; i < pred_size; i++){
              
              invjday(Predictions[i].jdstart ,config->timezone,config->daylight , year, mon, day, hr, min, sec);
              buf.add("|");buf.add(day);buf.add(" ");buf.add(monstr[mon]);
              buf.add("|");buf.addTime(hr,min,sec);
              buf.add("|");buf.add(Predictions[i].azstart);
              
              invjday(Predictions[i].jdmax ,config->timezone,config->daylight , year, mon, day, hr, min, sec);
              buf.add("°|");buf.addTime(hr,min,sec);
              buf.add("|");buf.add(Predictions[i].maxelevation);
              
              invjday(Predictions[i].jdstop ,config->timezone,config->daylight , year, mon, day, hr, min, sec);
              buf.add("°|");buf.addTime(hr,min,sec);
              buf.add("|");buf.add(Predictions[i].azstop);
              
              switch(Predictions[i].sight){
                    case lighted:
                        buf.add("°|Visible");
                        break;
                    case eclipsed:
                        buf.add("°|Eclipsed");
                        break;
                    case daylight:
                        buf.add("°|Daylight");
                        break;
                }
            }
            buf.add("\ninput|pre|");buf.add(getUnixFromJulian(Predictions[0].jdmax));
            buf.add("\ninput|next|");buf.add(getUnixFromJulian(Predictions[pred_size-1].jdmax));
        }else{
            buf.add("tab|pass|1|1|Error");
            buf.add("\ninput|pre|0");
            buf.add("\ninput|next|0");
        }
        buf.add("\ndiv|line1|");buf.add(sat.line1);
        buf.add("\ndiv|line2|");buf.add(sat.line2);
        invjday(sat.satrec.jdsatepoch ,config->timezone,config->daylight , year, mon, day, hr, min, sec);
        buf.add("\ndiv|epoch|");buf.add(day);buf.add("/");buf.add(mon);buf.add("/");buf.add(year);buf.add(" ");buf.addTime(hr,min,sec);
        buf.add("\ndiv|sat|");buf.add(sat.satName);
        
        server.send ( 200, "text/plain", buf.getPointer());
    
        #ifdef DEBUG
          Serial.println("Send /data");
        #endif
    
        if (calc){
          delete Predictions;
        }
      
    }else{
        server.send ( 400, "text/html", "Page not Found" );
    }
}

void sendconfig(){

#ifdef USE_SETTINGS_PW
    if ( WiFi.getMode() != WIFI_AP){
      if ( !server.authenticate(host,ap_password)){
        server.requestAuthentication();
        return;
      }
    }
#endif
    Stringbuffer buf(43 + 75 + 15*12 + 16*4 + 19*6 + 138 + 13 + 16*3 +  20);
    
    buf.add("input|SSID|");buf.add(config->ssid);
    buf.add("\ninput|PSK|");buf.add(config->password);
    buf.add("\ninput|ip_0|");buf.add(config->IP[0]);
    buf.add("\ninput|ip_1|");buf.add(config->IP[1]);
    buf.add("\ninput|ip_2|");buf.add(config->IP[2]);
    buf.add("\ninput|ip_3|");buf.add(config->IP[3]);
    buf.add("\ninput|nm_0|");buf.add(config->Netmask[0]);
    buf.add("\ninput|nm_1|");buf.add(config->Netmask[1]);
    buf.add("\ninput|nm_2|");buf.add(config->Netmask[2]);
    buf.add("\ninput|nm_3|");buf.add(config->Netmask[3]);
    buf.add("\ninput|gw_0|");buf.add(config->Gateway[0]);
    buf.add("\ninput|gw_1|");buf.add(config->Gateway[1]);
    buf.add("\ninput|gw_2|");buf.add(config->Gateway[2]);
    buf.add("\ninput|gw_3|");buf.add(config->Gateway[3]);

    buf.add("\ninput|lat|");buf.add(config->lat);
    buf.add("\ninput|lon|");buf.add(config->lon);
    buf.add("\ninput|alt|");buf.add(config->alt);
    buf.add("\ninput|sat|");buf.add(config->satnum);

    buf.add("\ninput|VisL|");buf.addColor(config->ColorVisL);
    buf.add("\ninput|VisH|");buf.addColor(config->ColorVisH);
    buf.add("\ninput|EclL|");buf.addColor(config->ColorEclL);
    buf.add("\ninput|EclH|");buf.addColor(config->ColorEclH);
    buf.add("\ninput|DayL|");buf.addColor(config->ColorDayL);
    buf.add("\ninput|DayH|");buf.addColor(config->ColorDayH);

    buf.add("\ninput|ts|");buf.add(config->ntpServerName);
    buf.add("\ninput|tz|");buf.add(config->timezone);
    
    buf.add("\nchk|ds|");buf.add((config->daylight ? "checked" : ""));
    buf.add("\nchk|DHCP|");buf.add((config->dhcp ? "checked" : ""));
    
    server.send ( 200, "text/plain", buf.getPointer() );

    #ifdef DEBUG
      Serial.println("Send /config");
    #endif
}

////////////////////////////////////
//      Login functions           //
////////////////////////////////////
/**
void randomid(char p[]){
   for (int i=0;i<10;i++){
      p[i] = (char)random(65,90);
   }
   p[10] = '\0';
}
***/
////////////////////////////////////
//      Init functions            //
////////////////////////////////////

void initServer(){
  
    server.on("/", [](){sendbestand("/home.html","text/html");});
    server.on("/home.html", [](){sendbestand("/home.html","text/html");});
    server.on("/links.html", [](){sendbestand("/links.html","text/html");});
    server.on("/about.html", [](){sendbestand("/about.html","text/html");});
    server.on("/w3.css", [](){sendbestand("/w3.css","text/css");});
    server.on("/favicon.ico", [](){sendbestand("/favicon.ico","image/ico");});
    server.on("/microajax.js", [](){sendbestand("/microajax.js","text/javascript");});
    server.on("/config",sendconfig);
    server.on("/data", senddata);
    
    server.on("/settings.html", [](){

#ifdef USE_SETTINGS_PW
        if ( WiFi.getMode() != WIFI_AP){
          if ( !server.authenticate(host,ap_password)){
            server.requestAuthentication();
            return;
          }
        }
#endif
        saveNetworkSettings();
        sendbestand("/settings.html","text/html");
        
    });
    
    
    server.begin();
    
}

void initWebsocket(){
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void initMonths(){
 
    strcpy(monstr[1], "Jan");
    strcpy(monstr[2], "Feb");
    strcpy(monstr[3], "Mar");
    strcpy(monstr[4], "Apr");
    strcpy(monstr[5], "May");
    strcpy(monstr[6], "Jun");
    strcpy(monstr[7], "Jul");
    strcpy(monstr[8], "Aug");
    strcpy(monstr[9], "Sep");
    strcpy(monstr[10], "Oct");
    strcpy(monstr[11], "Nov");
    strcpy(monstr[12], "Dec");

}

void AnimStop();

void initOTA(){
  
    ArduinoOTA.onStart([]() {
      #ifdef DEBUG
        Serial.println("OTA Start");
      #endif
      #ifdef DEBUG_frame
        ticker.detach();
      #endif
      
      closeAllConnections();
      
      LedStrip.AnimStop();
      
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
       int pix = (progress / (total / PIXELS));
       RgbColor color = RgbColor(127,0,255);
       strip.SetPixelColor(pix, color);
       strip.Show();
    });
    
    
    
    ArduinoOTA.onError([](ota_error_t error) {
      #ifdef DEBUG
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      #endif

      LedStrip.SetAnimColor(0x9f,0x0,0x0);
      LedStrip.AnimStart(ANIM_FLASH);

      delay(3000);
      ESP.restart();
      
    });

    #ifdef DEBUG
      ArduinoOTA.onEnd([]() {
        Serial.println("OTA End");
      });
    #endif
    
    ArduinoOTA.setHostname(host);
    ArduinoOTA.begin();
}

#endif

