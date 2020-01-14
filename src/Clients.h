#ifndef clients_h
#define clients_h


////////////////////////////////////////////////////////
//                  NTP server stuff                  //
////////////////////////////////////////////////////////


const int NTP_PACKET_SIZE = 48; 
byte packetBuffer[ NTP_PACKET_SIZE]; 


unsigned long NTPRefresh()
{

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress timeServerIP; 
    WiFi.hostByName(config->ntpServerName, timeServerIP); 
    //sendNTPpacket(timeServerIP); // send an NTP packet to a time server

    #ifdef DEBUG
      Serial.println("sending NTP packet...");
    #endif
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;
    UDPNTPClient.beginPacket(timeServerIP, 123); 
    UDPNTPClient.write(packetBuffer, NTP_PACKET_SIZE);
    UDPNTPClient.endPacket();

    int cb = 0;
    int i = 0;
    do{
        delay(1);
        i++;
        cb = UDPNTPClient.parsePacket();
    }while (cb == 0 && i < 1000);
    
    if (!cb) {
      #ifdef DEBUG
        Serial.println("NTP no packet yet");
      #endif
      return 0;
    }
    else 
    { 
      #ifdef DEBUG
        Serial.print("NTP packet received, length=");
        Serial.println(cb);
      #endif
      UDPNTPClient.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      const unsigned long seventyYears = 2208988800UL;
      unsigned long epoch = secsSince1900 - seventyYears;
      return epoch;
    }
  }
  return 0;
}



///////////////////////////////////////////////
//          getting tle data                 //
///////////////////////////////////////////////
bool getTle(int ide){
  return getTle(ide, false);
}

bool MatchTlePattern(const char* line,const char* pattern){
    int i = 0;
    bool match = true;
    while (pattern[i] != 0 && line[i] != 0 && match){
      switch(pattern[i]){
          case 'N':
            match = line[i] >= '0' && line[i] <= '9' || line[i]  == ' ';
            break;
          case 'A':
            match = line[i] >= 'A' && line[i] <= 'Z' || line[i]  == ' ';
            break;
          case 'C':
            match = line[i] >= 'A' && line[i] <= 'Z';
            break;
          case '+':
            match = line[i] == '+' || line[i] == '-' || line[i]  == ' ';
            break;
          default:
            match = line[i] == pattern[i];
            break;
      }
      i++;
    }
    return match && pattern[i] == 0;
}

bool getTle(int ide, bool forceupdate){

  if (WiFi.status() == WL_CONNECTED){

    client.flush();

    const int httpPort = 80;
    if (!client.connect("celestrak.com", httpPort)) {
      #ifdef DEBUG
        Serial.println("connection failed");
      #endif
      return false;
    }
    String line = "GET /satcat/tle.php?CATNR=" + String(ide)+ " HTTP/1.1\r\nHost: celestrak.com\r\nConnection: close\r\n\r\n";
    #ifdef DEBUG
      Serial.println(line);
    #endif
    client.print(line);
    int i=0;
    while (client.available() == false & i<5000) {
        delay(10);
        i+=1;
    }
    if (i>=5000){
      #ifdef DEBUG
        Serial.println("http timeout");
      #endif
      return false;
    }

    // Read all the lines of the reply from server and print them to Serial
    //read header
    bool succes = false;
    char* longstr1 = new char[80];
    char* longstr2 = new char[80];
    char* naam = new char[25];

    //Read lines until it match the first tle line pattern
    while (client.available() && succes == false){
      line = client.readStringUntil('\n');
      line.trim();
      #ifdef DEBUG
        Serial.println(line);
      #endif
      if (!succes){
        if (MatchTlePattern(line.c_str(),"1 NNNNNC NNNNNAAA NNNNN.NNNNNNNN +.NNNNNNNN +NNNNN+N +NNNNN+N N NNNNN")){
          succes = true;
          strlcpy (longstr1, line.c_str(),80);
        } else {
          strlcpy (naam, line.c_str(),25);    //Store line into name until succes match
        }
      }
    }
    //Read second line of tle
    if (client.available() && succes){
      line = client.readStringUntil('\n');
      line.trim();
      #ifdef DEBUG
        Serial.println(line);
      #endif
      if (MatchTlePattern(line.c_str(),"2 NNNNN NNN.NNNN NNN.NNNN NNNNNNN NNN.NNNN NNN.NNNN NN.NNNNNNNNNNNNNN")){
        succes = true;
        strlcpy (longstr2, line.c_str(),80);
      }
    }

    //Stop and cleanup if not succesfull
    if (!succes){
      #ifdef DEBUG
        Serial.println("\r\nnot a correct api-response");
      #endif
      delete[] naam;
      delete[] longstr1;
      delete[] longstr2;
      return false;
    }

    client.stop();

    #ifdef DEBUG
      Serial.println("------Tle matched-----");
      Serial.println(naam);
      Serial.println(longstr1);
      Serial.println(longstr2);
      Serial.flush();   //wait until data is send, it cause a wdt reset because sat.init changes the char[]
    #endif

    if ( !twolineChecksum(longstr1) || !twolineChecksum(longstr2)){
        #ifdef DEBUG
          Serial.println("Checksum failed");
        #endif
        delete[] naam;
        delete[] longstr1;
        delete[] longstr2;
        return false;
    }
    
    bool satupdate = sat.init(naam,longstr1,longstr2);
    delete[] naam;
    delete[] longstr1;
    delete[] longstr2;
    
    if( satupdate || forceupdate){   
      predError = !predictPasses();
      uint8_t buf[]="n";webSocket.broadcastBIN(buf,1); ///update websites
      calcOrbit();
      webSocketSendOrbit(); 
    }
    
    return true;
  }
  return false;
}

/////////////////////////////////////
//          update data            //
/////////////////////////////////////

bool updateTime(){
  unsigned long temptime = NTPRefresh();
  if (temptime){
    unixtime = temptime;
    timemillis = millis();
    jdtime = getJulianFromUnix(unixtime);
    updatejdtime = jdtime + (2000.0 + random(2000))/24000.0;  //update in 2-4 hours
    
    #ifdef DEBUG
      int year,mon,day,hr,min;
      double sec;
      invjday(jdtime,config->timezone,config->daylight, year, mon, day, hr, min, sec);
      Serial.println("Current time: " + String(day) + '/' + String(mon) + '/' + String(year) + ' ' + String(hr) + ':' + String(min) + ':' + String(sec));
    #endif
    
    return true;
  }else{
    updatejdtime = jdtime + 0.010417; //retry in 15 min
    return false;
  }
}


#endif
