#pragma once
// Dedicated SSE socket task. Never holds the main WebServer/USB worker open.
// ponytail: three LAN dashboards; use server fanout for many remote viewers.
static WiFiServer eventServer(81);
static String realtimeJSON() {
  auto u=readUSB();
  return String("{\"firmware\":")+firmwareJSON()+",\"status_source\":\"esp32\",\"server_queue\":"+serverQueueSnapshot()+",\"queue_command\":"+queueCommandJSON()+",\"capt\":"+captTelemetryJSON()+",\"job_control\":"+jobStatusJSON()+",\"cloud\":"+cloudStatusJSON()+
    ",\"usb\":{\"connected\":"+(u.connected?"true":"false")+",\"responsive\":"+(u.responsive?"true":"false")+
    ",\"recovering\":"+(u.recovering?"true":"false")+",\"error\":"+u.error+"},\"tcp\":{\"state\":"+captTCPState.load()+
    ",\"total\":"+captTCPTotal.load()+",\"completed\":"+captTCPCompleted.load()+"}}";
}
static String realtimeKey(const String &body) {
  auto *j=cJSON_Parse(body.c_str());if(!j)return "";
  cJSON_DeleteItemFromObject(cJSON_GetObjectItem(j,"capt"),"age_ms");
  cJSON_DeleteItemFromObject(cJSON_GetObjectItem(j,"server_queue"),"age_ms");
  cJSON_DeleteItemFromObject(cJSON_GetObjectItem(j,"cloud"),"http");
  char *p=cJSON_PrintUnformatted(j);String key=p?String(p):String("");free(p);cJSON_Delete(j);return key;
}
static void realtimeTask(void *) {
  struct Viewer {WiFiClient socket;String header;uint32_t at=0;bool ready=false;};Viewer viewers[3];
  eventServer.begin();String previous;uint32_t tick=0,keepalive=0,seq=0;
  while(true){
    auto incoming=eventServer.accept();
    if(incoming){bool placed=false;for(auto &v:viewers)if(!v.socket.connected()){
      v.socket.stop();v.socket=incoming;v.header="";v.at=millis();v.ready=false;placed=true;break;
    }if(!placed)incoming.stop();}
    for(auto &v:viewers){
      if(!v.socket.connected())continue;
      if(!v.ready){
        while(v.socket.available() && v.header.length()<1024)v.header+=char(v.socket.read());
        if(v.header.indexOf("\r\n\r\n")<0){if(v.header.length()>=1024||uint32_t(millis()-v.at)>2000)v.socket.stop();continue;}
        uint32_t mask=uint32_t(WiFi.subnetMask());
        String expected="GET /events?token="+csrf+" HTTP/1.1\r\n";
        if(!WiFi.isConnected() || (uint32_t(v.socket.remoteIP())&mask)!=(uint32_t(WiFi.localIP())&mask) || !v.header.startsWith(expected)){v.socket.stop();continue;}
        v.socket.setNoDelay(true);
        String origin="http://"+WiFi.localIP().toString();
        // Only the IP dashboard origin can read events cross-port.
        v.socket.print("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: "+origin+"\r\nConnection: keep-alive\r\n\r\nretry: 500\n\ndata: "+realtimeJSON()+"\n\n");
        v.header="";v.ready=true;
      }
    }
    if(uint32_t(millis()-tick)>=250){
      tick=millis();String body=realtimeJSON(),key=realtimeKey(body);
      bool changed=key!=previous,heartbeat=uint32_t(millis()-keepalive)>=1000;
      if(changed||heartbeat){
        String frame=changed?"id: "+String(++seq)+"\ndata: "+body+"\n\n":String(": keepalive\n\n");
        for(auto &v:viewers)if(v.ready&&v.socket.connected())if(v.socket.write((const uint8_t *)frame.c_str(),frame.length())!=frame.length())v.socket.stop();
        previous=key;keepalive=millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
