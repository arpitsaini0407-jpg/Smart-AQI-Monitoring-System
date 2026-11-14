/* Smart_AQI_Monitor.ino
   ESP32 Offline AQI Monitor v3.5
   
   v3.5 Fix: 
   - JS-driven layout: reads window.innerHeight, sets all heights via JS
   - Works in Fully Kiosk Browser, Chrome, any screen
   - All charts always visible including AQI/IAQ
   - No vh/dvh CSS dependency - pure JS pixel calculation
*/

#include <WiFi.h>
#include <Wire.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_BME680.h>
#include <MQUnifiedsensor.h>
#include <LittleFS.h>

struct Breakpoint{float Cp_lo,Cp_hi;int I_lo,I_hi;};
const char* AP_SSID="AQI_MONITOR";
const char* AP_PASS="CHANGE_THIS_PASSWORD"; // Set your own Wi-Fi AP password before uploading.
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define PMS_RX 16
#define PMS_TX 17
HardwareSerial pmsSerial(2);
struct PMSData{uint16_t pm1_0=0,pm2_5=0,pm10=0;};
bool pmsOK=false;
unsigned long lastPMSSuccess=0;
const unsigned long PMS_TIMEOUT=10000;
unsigned long lastPMSFlush=0;
const unsigned long PMS_FLUSH_INTERVAL=30000;

#define BME_SDA 21
#define BME_SCL 22
Adafruit_BME680 bme;
bool bmeOK=false,bmeInitialized=false;
const float HUMIDITY_OFFSET=-3.0,TEMPERATURE_OFFSET=-0.5;

#define MQ_PIN 34
MQUnifiedsensor MQ135("ESP-32",3.3,12,MQ_PIN,"MQ-135");
bool mqOK=false;
const float MQ135_R0=78.84;
const float CO2_BASELINE=400.0,CO2_SCALE_FACTOR=0.005,CO2_MAX_INDOOR=500.0;
const float NH3_SCALE_FACTOR=0.002,NH3_MAX=6.0;
const int ADC_LOW_THRESHOLD=100,ADC_HIGH_THRESHOLD=3800,ADC_NUM_SAMPLES=5;

float gasResistanceBaseline=100000.0,humidityBaseline=40.0;
const int IAQ_BASELINE_SAMPLES=50;
float gasBaselineBuffer[50];
int gasBaselineIdx=0,gasBaselineCount=0;
bool gasBaselineReady=false;

const float EMA_ALPHA_FAST=0.3,EMA_ALPHA_MED=0.4,EMA_ALPHA_SLOW=0.5;
float prev_temperature=0,prev_humidity=0,prev_pressure=0,prev_gas_resistance=0;
float prev_co2=400.0,prev_nh3=0,prev_pm1_0=0,prev_pm2_5=0,prev_pm10=0,prev_iaq=50.0;
bool ema_initialized=false;

const int BL=60;
float buf_temp[BL],buf_hum[BL],buf_pres[BL],buf_co2[BL],buf_pm25[BL],buf_pm10[BL],buf_iaq[BL],buf_aqi[BL],buf_nh3[BL];
int buf_idx=0,buf_count=0;

float temperature=0,humidity=0,pressure_hpa=0,co2_ppm=400,nh3_ppm=0;
float pm1_0=0,pm2_5=0,pm10_val=0,iaq=50,final_aqi=0,gas_resistance=0;
String aqi_cat="Unknown",iaq_cat="Unknown";

unsigned long lastSensorRead=0;
const unsigned long SENSOR_INTERVAL=3000;
unsigned long lastLogWrite=0;
const unsigned long LOG_INTERVAL=60000;
const char* LOG_PATH="/data.csv";

float emaSmooth(float n,float p,float a){if(isnan(n)||isinf(n))return p;return a*n+(1-a)*p;}
float validateRange(float v,float mn,float mx,float fb){if(isnan(v)||isinf(v))return fb;return v<mn?mn:(v>mx?mx:v);}

bool readPMS(PMSData&d){
  while(pmsSerial.available()>=32){
    if((uint8_t)pmsSerial.peek()!=0x42){pmsSerial.read();continue;}
    uint8_t f[32];if(pmsSerial.readBytes(f,32)!=32)return false;
    if(f[0]!=0x42||f[1]!=0x4D)continue;
    uint16_t cs=0;for(int i=0;i<30;i++)cs+=f[i];
    if(cs!=(((uint16_t)f[30]<<8)|f[31]))continue;
    d.pm1_0=((uint16_t)f[10]<<8)|f[11];d.pm2_5=((uint16_t)f[12]<<8)|f[13];d.pm10=((uint16_t)f[14]<<8)|f[15];
    if(d.pm2_5>1000||d.pm10>1000||d.pm1_0>1000)continue;return true;
  }return false;
}
void flushPMSBuffer(){while(pmsSerial.available())pmsSerial.read();}

const Breakpoint bp25[]={{0,30,0,50},{30.1,60,51,100},{60.1,90,101,200},{90.1,120,201,300},{120.1,250,301,400},{250.1,500,401,500}};
const Breakpoint bp10[]={{0,50,0,50},{50.1,100,51,100},{100.1,250,101,200},{250.1,350,201,300},{350.1,430,301,400},{430.1,1000,401,500}};
const Breakpoint bnh[]={{0,200,0,50},{200.1,400,51,100},{400.1,800,101,200},{800.1,1200,201,300},{1200.1,1800,301,400},{1800.1,2400,401,500}};

float subIndex(const Breakpoint*b,int n,float C){if(C<0)C=0;for(int i=0;i<n;i++)if(C>=b[i].Cp_lo&&C<=b[i].Cp_hi)return(float)(b[i].I_hi-b[i].I_lo)/(b[i].Cp_hi-b[i].Cp_lo)*(C-b[i].Cp_lo)+b[i].I_lo;return 500;}
String aqiCat(float a){if(a<=50)return"Good";if(a<=100)return"Satisfactory";if(a<=200)return"Moderate";if(a<=300)return"Poor";if(a<=400)return"Very Poor";return"Severe";}
String iaqCat(float v){if(v<=50)return"Excellent";if(v<=100)return"Good";if(v<=150)return"Lightly Polluted";if(v<=200)return"Moderately Polluted";if(v<=250)return"Heavily Polluted";if(v<=350)return"Severely Polluted";return"Extremely Polluted";}

void updateGasBaseline(float g){
  if(g<=0)return;gasBaselineBuffer[gasBaselineIdx]=g;gasBaselineIdx=(gasBaselineIdx+1)%IAQ_BASELINE_SAMPLES;
  if(gasBaselineCount<IAQ_BASELINE_SAMPLES)gasBaselineCount++;
  if(gasBaselineCount>=10){gasBaselineReady=true;float s[50];for(int i=0;i<gasBaselineCount;i++)s[i]=gasBaselineBuffer[i];
    for(int i=0;i<gasBaselineCount-1;i++)for(int j=0;j<gasBaselineCount-i-1;j++)if(s[j]<s[j+1]){float t=s[j];s[j]=s[j+1];s[j+1]=t;}
    int t=max(1,gasBaselineCount/5);float sm=0;for(int i=0;i<t;i++)sm+=s[i];gasResistanceBaseline=sm/t;}
}

float calcIAQ(float g,float h){
  float gs;if(g<=0||gasResistanceBaseline<=0)gs=75;else{float r=g/gasResistanceBaseline;if(r>=1)gs=max(0.0f,50.0f*(2.0f-r));else if(r>=0.6)gs=50+(1-r)/0.4*50;else if(r>=0.3)gs=100+(0.6-r)/0.3*100;else if(r>=0.1)gs=200+(0.3-r)/0.2*150;else gs=350+(0.1-r)/0.1*150;}
  gs=validateRange(gs,0,500,75);float hd=abs(h-humidityBaseline),hs;
  if(hd<=5)hs=0;else if(hd<=10)hs=(hd-5)/5*25;else if(hd<=20)hs=25+(hd-10)/10*50;else if(hd<=30)hs=75+(hd-20)/10*75;else hs=150+(hd-30)/10*50;
  return validateRange(gs*0.75+validateRange(hs,0,200,0)*0.25,0,500,50);
}

void pushBuf(){buf_temp[buf_idx]=temperature;buf_hum[buf_idx]=humidity;buf_pres[buf_idx]=pressure_hpa;buf_co2[buf_idx]=co2_ppm;buf_nh3[buf_idx]=nh3_ppm;buf_pm25[buf_idx]=pm2_5;buf_pm10[buf_idx]=pm10_val;buf_iaq[buf_idx]=iaq;buf_aqi[buf_idx]=final_aqi;buf_idx=(buf_idx+1)%BL;if(buf_count<BL)buf_count++;}

void ensureLogHeader(){if(!LittleFS.exists(LOG_PATH)){File f=LittleFS.open(LOG_PATH,"w");if(f){f.println("uptime_s,temp,hum,pres,pm1,pm25,pm10,co2,nh3,gas,gas_base,iaq,iaq_cat,aqi,aqi_cat");f.close();}}}
void appendLog(){ensureLogHeader();File f=LittleFS.open(LOG_PATH,"a");if(!f)return;f.printf("%lu,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.1f,%.2f,%.0f,%.0f,%.1f,%s,%.1f,%s\n",millis()/1000,temperature,humidity,pressure_hpa,pm1_0,pm2_5,pm10_val,co2_ppm,nh3_ppm,gas_resistance,gasResistanceBaseline,iaq,iaq_cat.c_str(),final_aqi,aqi_cat.c_str());f.close();File c=LittleFS.open(LOG_PATH,"r");if(c){if(c.size()>100000){c.close();LittleFS.remove(LOG_PATH);ensureLogHeader();}else c.close();}}
bool mqConn(){int s=0;for(int i=0;i<ADC_NUM_SAMPLES;i++){s+=analogRead(MQ_PIN);delay(2);}int r=s/ADC_NUM_SAMPLES;return r>=ADC_LOW_THRESHOLD&&r<=ADC_HIGH_THRESHOLD;}

String buildJSON(){
  String j="{\"uptime\":"+String(millis()/1000)+",\"temperature\":"+String(temperature,1)+",\"humidity\":"+String(humidity,1)+",\"pressure\":"+String(pressure_hpa,1)+",\"pm1_0\":"+String(pm1_0,0)+",\"pm25\":"+String(pm2_5,0)+",\"pm10\":"+String(pm10_val,0)+",\"co2\":"+String(co2_ppm,1)+",\"nh3\":"+String(nh3_ppm,2)+",\"gas_res\":"+String(gas_resistance,0)+",\"iaq\":"+String(iaq,1)+",\"iaq_cat\":\""+iaq_cat+"\",\"aqi\":"+String(final_aqi,1)+",\"aqi_cat\":\""+aqi_cat+"\",\"pms_ok\":"+(pmsOK?"true":"false")+",\"bme_ok\":"+(bmeOK?"true":"false")+",\"mq_ok\":"+(mqOK?"true":"false")+"}";
  return j;
}
void notifyClients(){if(ws.count()==0)return;ws.textAll(buildJSON());}

void readSensorsOnce(){
  if(bme.performReading()){bmeOK=true;float rt=validateRange(bme.temperature+TEMPERATURE_OFFSET,-40,85,prev_temperature);float rh=validateRange(bme.humidity+HUMIDITY_OFFSET,0,100,prev_humidity);float rp=validateRange(bme.pressure/100.0,300,1100,prev_pressure);float rg=validateRange(bme.gas_resistance,0,1e6,prev_gas_resistance);if(!ema_initialized){temperature=rt;humidity=rh;pressure_hpa=rp;gas_resistance=rg;}else{temperature=emaSmooth(rt,prev_temperature,EMA_ALPHA_SLOW);humidity=emaSmooth(rh,prev_humidity,EMA_ALPHA_SLOW);pressure_hpa=emaSmooth(rp,prev_pressure,EMA_ALPHA_SLOW);gas_resistance=emaSmooth(rg,prev_gas_resistance,EMA_ALPHA_MED);}prev_temperature=temperature;prev_humidity=humidity;prev_pressure=pressure_hpa;prev_gas_resistance=gas_resistance;bmeInitialized=true;updateGasBaseline(gas_resistance);}else{bmeOK=false;temperature=0;humidity=0;pressure_hpa=0;gas_resistance=0;}
  if(mqConn()){MQ135.update();MQ135.setA(110.3);MQ135.setB(-2.769);float cs=MQ135.readSensor();float cr=CO2_BASELINE+cs*CO2_SCALE_FACTOR;cr=validateRange(cr,0,CO2_MAX_INDOOR,prev_co2);if(ema_initialized&&abs(cr-prev_co2)>20)cr=prev_co2;co2_ppm=ema_initialized?emaSmooth(cr,prev_co2,EMA_ALPHA_FAST):cr;prev_co2=co2_ppm;MQ135.setA(102.2);MQ135.setB(-2.473);float ns=MQ135.readSensor();float nr=ns*NH3_SCALE_FACTOR;nr=validateRange(nr,0,NH3_MAX,prev_nh3);if(ema_initialized&&abs(nr-prev_nh3)>1)nr=prev_nh3;nh3_ppm=ema_initialized?emaSmooth(nr,prev_nh3,EMA_ALPHA_FAST):nr;prev_nh3=nh3_ppm;mqOK=true;}else{co2_ppm=0;nh3_ppm=0;prev_co2=400;prev_nh3=0;mqOK=false;}
  if(millis()-lastPMSFlush>PMS_FLUSH_INTERVAL&&!pmsOK){flushPMSBuffer();lastPMSFlush=millis();}PMSData tmp;bool got=false;int at=0;while(readPMS(tmp)&&at<3){got=true;at++;}
  if(got){pmsOK=true;lastPMSSuccess=millis();float r1=validateRange((float)tmp.pm1_0,0,1000,prev_pm1_0);float r25=validateRange((float)tmp.pm2_5,0,1000,prev_pm2_5);float r10=validateRange((float)tmp.pm10,0,1000,prev_pm10);if(r10<r25)r10=r25;if(r25<r1)r25=r1;if(ema_initialized){if(abs(r25-prev_pm2_5)>100)r25=prev_pm2_5;if(abs(r10-prev_pm10)>150)r10=prev_pm10;}if(!ema_initialized){pm1_0=r1;pm2_5=r25;pm10_val=r10;}else{pm1_0=emaSmooth(r1,prev_pm1_0,EMA_ALPHA_MED);pm2_5=emaSmooth(r25,prev_pm2_5,EMA_ALPHA_MED);pm10_val=emaSmooth(r10,prev_pm10,EMA_ALPHA_MED);}prev_pm1_0=pm1_0;prev_pm2_5=pm2_5;prev_pm10=pm10_val;}else{if(millis()-lastPMSSuccess>PMS_TIMEOUT){pmsOK=false;pm1_0=0;pm2_5=0;pm10_val=0;prev_pm1_0=0;prev_pm2_5=0;prev_pm10=0;}}
  if(!ema_initialized&&(bmeInitialized||mqOK||pmsOK))ema_initialized=true;
  if(bmeOK&&gas_resistance>0){float ri=calcIAQ(gas_resistance,humidity);iaq=ema_initialized?emaSmooth(ri,prev_iaq,EMA_ALPHA_MED):ri;prev_iaq=iaq;}else if(mqOK&&(co2_ppm>0||pm2_5>0)){float ri=50;if(co2_ppm>CO2_BASELINE)ri+=(co2_ppm-CO2_BASELINE)/100*50;ri+=pm2_5*0.3;ri=validateRange(ri,0,500,prev_iaq);iaq=emaSmooth(ri,prev_iaq,EMA_ALPHA_MED);prev_iaq=iaq;}else{iaq=50;prev_iaq=50;}
  iaq_cat=iaqCat(iaq);float s25=subIndex(bp25,6,pm2_5);float s10=subIndex(bp10,6,pm10_val);float sn=subIndex(bnh,6,nh3_ppm);final_aqi=validateRange(max(max(s25,s10),sn),0,500,0);aqi_cat=aqiCat(final_aqi);pushBuf();
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=1024">
<title>AQI Monitor</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
:root{--bg:#0b0f14;--bg2:#111827;--gb:rgba(255,255,255,0.1);--cb:rgba(255,255,255,0.04);--t1:#e2e8f0;--t2:#94a3b8;}
body.light{--bg:#f0f4f8;--bg2:#fff;--gb:rgba(0,0,0,0.1);--cb:rgba(255,255,255,0.85);--t1:#1e293b;--t2:#64748b;}
html,body{width:100%;height:100%;overflow:hidden;background:var(--bg);color:var(--t1);font-family:'Segoe UI',system-ui,sans-serif;}

#app{position:absolute;top:0;left:0;right:0;bottom:0;display:flex;flex-direction:column;overflow:hidden;}

/* HEADER */
#hdr{display:flex;align-items:center;justify-content:space-between;padding:0 12px;background:var(--bg2);border-bottom:1px solid var(--gb);flex-shrink:0;gap:6px;}
.hl h1{font-size:16px;font-weight:700;white-space:nowrap;}
.hl p{font-size:9px;color:var(--t2);}
.hm{display:flex;align-items:center;gap:8px;}
#uptD{font-size:11px;color:var(--t2);white-space:nowrap;}
.tw{display:flex;align-items:center;gap:4px;font-size:10px;color:var(--t2);}
.tgl{position:relative;width:32px;height:18px;border-radius:9px;background:rgba(255,255,255,0.1);border:1px solid var(--gb);cursor:pointer;}
.tgl::after{content:'';position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;background:#60a5fa;transition:transform .2s;}
.tgl.on::after{transform:translateX(14px);background:#facc15;}
.hr{display:flex;gap:4px;}
.btn{background:rgba(255,255,255,0.06);border:1px solid var(--gb);padding:3px 8px;border-radius:5px;color:var(--t1);cursor:pointer;font-size:10px;white-space:nowrap;}
.btn:hover{background:rgba(96,165,250,0.2);}

/* GRID */
#topGrid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:5px;padding:5px;flex-shrink:0;}
.card{background:var(--cb);border:1px solid var(--gb);border-radius:8px;padding:5px 7px;overflow:hidden;}
.card-hdr{display:flex;justify-content:space-between;align-items:baseline;}
.ct{font-size:9px;font-weight:600;color:var(--t2);text-transform:uppercase;letter-spacing:.3px;}
.cv{font-size:16px;font-weight:700;}
.cu{font-size:9px;color:var(--t2);font-weight:400;}
.cbox{width:100%;overflow:hidden;background:rgba(255,255,255,0.015);border-radius:4px;margin-top:3px;}
.cbox canvas{display:block;width:100%;}

.pm-grid{display:grid;grid-template-columns:1fr 1fr;gap:4px;}
.pm-half{text-align:center;}
.pm-half .cv{font-size:14px;}

/* BOTTOM */
#botRow{display:grid;grid-template-columns:1fr 1fr;gap:5px;padding:0 5px;flex-shrink:0;}
.bc{background:var(--cb);border:1px solid var(--gb);border-radius:8px;padding:5px 8px;overflow:hidden;}
.bc .cv{font-size:20px;}
.bc .cat{display:inline-block;padding:1px 6px;border-radius:10px;font-size:9px;font-weight:600;margin-left:4px;}

/* FOOTER */
#ftr{display:flex;align-items:center;justify-content:space-between;padding:2px 10px;border-top:1px solid var(--gb);font-size:9px;color:var(--t2);flex-shrink:0;gap:4px;}
.sbadges{display:flex;gap:4px;}
.badge{padding:1px 6px;border-radius:10px;font-size:8px;font-weight:600;}
.bok{background:rgba(52,211,153,0.15);color:#34d399;}
.berr{background:rgba(251,113,133,0.15);color:#fb7185;}

.cat-excellent{background:rgba(16,185,129,0.2);color:#10b981;}
.cat-good{background:rgba(52,211,153,0.2);color:#34d399;}
.cat-satisfactory{background:rgba(96,165,250,0.2);color:#60a5fa;}
.cat-light,.cat-moderate{background:rgba(250,204,21,0.2);color:#facc15;}
.cat-poor{background:rgba(249,115,22,0.2);color:#f97316;}
.cat-heavy,.cat-verypoor{background:rgba(251,113,133,0.2);color:#fb7185;}
.cat-severe{background:rgba(220,38,38,0.2);color:#ef4444;}
.cat-extreme{background:rgba(127,29,29,0.3);color:#fca5a5;}
</style>
</head>
<body>
<div id="app">

<div id="hdr">
  <div class="hl"><h1>&#127807; SMART AQI MONITOR</h1><p>Offline AP &#8226; Real-time</p></div>
  <div class="hm">
    <span id="uptD">0s</span>
    <div class="tw"><span>Dark</span><div class="tgl" id="tglBtn" onclick="TT()"></div><span>Light</span></div>
  </div>
  <div class="hr">
    <button class="btn" onclick="MR()">&#x27F3; Refresh</button>
    <button class="btn" onclick="DL()">&#x2B07; Logs</button>
    <button class="btn" onclick="CL()">&#x1F5D1; Clear</button>
  </div>
</div>

<div id="topGrid">
  <div class="card"><div class="card-hdr"><span class="ct">Pressure</span><span class="cv" id="pVal">--<span class="cu"> hPa</span></span></div><div class="cbox"><canvas id="c0"></canvas></div></div>
  <div class="card"><div class="card-hdr"><span class="ct">NH&#x2083;</span><span class="cv" id="nVal">--<span class="cu"> ppm</span></span></div><div class="cbox"><canvas id="c1"></canvas></div></div>
  <div class="card"><div class="card-hdr"><span class="ct">CO&#x2082;</span><span class="cv" id="cVal">--<span class="cu"> ppm</span></span></div><div class="cbox"><canvas id="c2"></canvas></div></div>
  <div class="card"><div class="card-hdr"><span class="ct">Temperature</span><span class="cv" id="tVal">--<span class="cu"> &#xB0;C</span></span></div><div class="cbox"><canvas id="c3"></canvas></div></div>
  <div class="card"><div class="card-hdr"><span class="ct">Humidity</span><span class="cv" id="hVal">--<span class="cu"> %</span></span></div><div class="cbox"><canvas id="c4"></canvas></div></div>
  <div class="card"><div class="ct" style="margin-bottom:2px">Particulate Matter</div><div class="pm-grid"><div class="pm-half"><div class="ct">PM2.5</div><div class="cv" id="p2Val">--<span class="cu"> &#xB5;g/m&#xB3;</span></div><div class="cbox"><canvas id="c5"></canvas></div></div><div class="pm-half"><div class="ct">PM10</div><div class="cv" id="p1Val">--<span class="cu"> &#xB5;g/m&#xB3;</span></div><div class="cbox"><canvas id="c6"></canvas></div></div></div></div>
</div>

<div id="botRow">
  <div class="bc"><div class="ct">AQI (Air Quality Index)</div><div><span class="cv" id="aqV">--</span><span class="cat" id="aqC">--</span></div><div class="cbox"><canvas id="c7"></canvas></div></div>
  <div class="bc"><div class="ct">IAQ (Indoor Air Quality)</div><div><span class="cv" id="iqV">--</span><span class="cat" id="iqC">--</span></div><div class="cbox"><canvas id="c8"></canvas></div></div>
</div>

<div id="ftr">
  <div class="sbadges"><span class="badge" id="bB">BME680:--</span><span class="badge" id="pB">PMS:--</span><span class="badge" id="mB">MQ:--</span></div>
  <span id="mem">Mem:--</span>
</div>

</div>

<script>
// ======= JS-DRIVEN LAYOUT =======
// Reads actual window.innerHeight, calculates pixel heights
// for header, grid rows, bottom row, footer, chart canvases.
// This is the ONLY reliable way for Fully Kiosk Browser.

function doLayout(){
  var W=window.innerWidth, H=window.innerHeight;
  // Heights: header=6%, footer=4%, gap=~2%, topGrid=56%, botRow=32%
  var hdrH=Math.max(32, Math.round(H*0.06));
  var ftrH=Math.max(20, Math.round(H*0.035));
  var gap=5;
  var botH=Math.max(80, Math.round(H*0.22));
  var topH=H - hdrH - ftrH - botH - gap*3;
  if(topH<100) topH=100;

  var hdr=document.getElementById('hdr');
  hdr.style.height=hdrH+'px';

  var ftr=document.getElementById('ftr');
  ftr.style.height=ftrH+'px';

  var tg=document.getElementById('topGrid');
  tg.style.height=topH+'px';
  // Each row = half of topGrid minus gaps
  var cardRowH=Math.floor((topH - gap)/2);
  var cardH=cardRowH - gap;
  var cards=tg.querySelectorAll('.card');
  for(var i=0;i<cards.length;i++) cards[i].style.height=cardH+'px';

  var br=document.getElementById('botRow');
  br.style.height=botH+'px';
  var bcs=br.querySelectorAll('.bc');
  for(var i=0;i<bcs.length;i++) bcs[i].style.height=(botH-gap)+'px';

  // Now set canvas heights
  var boxes=document.querySelectorAll('.cbox');
  for(var i=0;i<boxes.length;i++){
    var parent=boxes[i].parentElement;
    // cbox = parent height minus non-chart content
    var siblings=parent.children;
    var usedH=0;
    for(var j=0;j<siblings.length;j++){
      if(siblings[j]!==boxes[i]){
        usedH+=siblings[j].offsetHeight||0;
      }
    }
    // For pm-half, measure differently
    if(parent.classList.contains('pm-half')){
      var pmGrid=parent.parentElement;
      var pmCard=pmGrid.parentElement;
      var pmTitle=pmCard.querySelector('.ct');
      var availH=cardH - (pmTitle?pmTitle.offsetHeight:0) - 10;
      var pmUsed=0;
      var ch=parent.children;
      for(var j=0;j<ch.length;j++){
        if(ch[j]!==boxes[i]) pmUsed+=ch[j].offsetHeight||0;
      }
      var cbH=Math.max(20, availH - pmUsed - 6);
      boxes[i].style.height=cbH+'px';
    } else {
      var cbH=Math.max(20, (parent.classList.contains('bc')?(botH-gap):cardH) - usedH - 12);
      boxes[i].style.height=cbH+'px';
    }
    // Set canvas pixel size
    var cv=boxes[i].querySelector('canvas');
    if(cv){
      var bw=boxes[i].offsetWidth||boxes[i].clientWidth||200;
      var bh=parseInt(boxes[i].style.height)||40;
      cv.style.width=bw+'px';
      cv.style.height=bh+'px';
    }
  }
  drawAll();
}

window.addEventListener('load',function(){
  doLayout();
  setTimeout(doLayout,200);
  setTimeout(doLayout,600);
  setTimeout(doLayout,1500);
});
window.addEventListener('resize',function(){setTimeout(doLayout,50);});

// ======= SPARKLINE =======
var N=60,dT=[],dH=[],dP=[],dC=[],dN=[],dP2=[],dP1=[],dI=[],dA=[];
function pa(a,v){a.push(v);if(a.length>N)a.shift();}

function spark(id,arr,col,fill){
  var cv=document.getElementById(id);if(!cv)return;
  var W=cv.clientWidth||parseInt(cv.style.width)||cv.parentElement.offsetWidth;
  var H=cv.clientHeight||parseInt(cv.style.height)||cv.parentElement.offsetHeight;
  if(W<4||H<4)return;
  var dpr=window.devicePixelRatio||1;
  cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr);
  var ctx=cv.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,W,H);
  if(arr.length<2)return;
  var mn=1e9,mx=-1e9;for(var i=0;i<arr.length;i++){if(arr[i]<mn)mn=arr[i];if(arr[i]>mx)mx=arr[i];}
  if(mn===mx){mn-=1;mx+=1;}var rng=mx-mn;mn-=rng*.05;mx+=rng*.05;
  var pts=[];for(var i=0;i<arr.length;i++){pts.push({x:arr.length>1?i/(arr.length-1)*W:0,y:H-((arr[i]-mn)/(mx-mn))*(H-4)-2});}
  if(fill){ctx.beginPath();ctx.moveTo(pts[0].x,H);ctx.lineTo(pts[0].x,pts[0].y);for(var i=1;i<pts.length;i++){var xc=(pts[i-1].x+pts[i].x)/2,yc=(pts[i-1].y+pts[i].y)/2;ctx.quadraticCurveTo(pts[i-1].x,pts[i-1].y,xc,yc);}ctx.lineTo(pts[pts.length-1].x,pts[pts.length-1].y);ctx.lineTo(pts[pts.length-1].x,H);ctx.closePath();ctx.fillStyle=fill;ctx.fill();}
  ctx.beginPath();ctx.lineWidth=1.5;ctx.strokeStyle=col;ctx.lineJoin='round';ctx.lineCap='round';ctx.moveTo(pts[0].x,pts[0].y);for(var i=1;i<pts.length;i++){var xc=(pts[i-1].x+pts[i].x)/2,yc=(pts[i-1].y+pts[i].y)/2;ctx.quadraticCurveTo(pts[i-1].x,pts[i-1].y,xc,yc);}ctx.lineTo(pts[pts.length-1].x,pts[pts.length-1].y);ctx.stroke();
  var l=pts[pts.length-1];ctx.beginPath();ctx.arc(l.x,l.y,2.5,0,6.28);ctx.fillStyle=col;ctx.fill();
}

function drawAll(){
  spark('c0',dP,'#93c5fd','rgba(147,197,253,0.12)');
  spark('c1',dN,'#facc15','rgba(250,204,21,0.12)');
  spark('c2',dC,'#34d399','rgba(52,211,153,0.12)');
  spark('c3',dT,'#60a5fa','rgba(96,165,250,0.12)');
  spark('c4',dH,'#7dd3fc','rgba(125,211,252,0.12)');
  spark('c5',dP2,'#fb7185','rgba(251,113,133,0.12)');
  spark('c6',dP1,'#f97316','rgba(249,115,22,0.12)');
  spark('c7',dA,'#a78bfa','rgba(167,139,250,0.15)');
  spark('c8',dI,'#2dd4bf','rgba(45,212,191,0.15)');
}

// ======= WEBSOCKET =======
var ws,isL=false,mP=false;
function initWS(){ws=new WebSocket('ws://'+location.hostname+'/ws');ws.onclose=function(){setTimeout(initWS,3000);};ws.onmessage=function(e){try{onD(JSON.parse(e.data));}catch(x){}};}
window.addEventListener('load',initWS);

function getCat(c){c=(c||'').toLowerCase();if(c==='excellent')return'cat-excellent';if(c==='good')return'cat-good';if(c==='satisfactory')return'cat-satisfactory';if(c.indexOf('lightly')>=0)return'cat-light';if(c.indexOf('moderately')>=0||c==='moderate')return'cat-moderate';if(c==='poor')return'cat-poor';if(c.indexOf('heavily')>=0)return'cat-heavy';if(c.indexOf('very')>=0)return'cat-verypoor';if(c.indexOf('severely')>=0||c==='severe')return'cat-severe';if(c.indexOf('extremely')>=0)return'cat-extreme';return'cat-moderate';}
function fU(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;if(h>0)return h+'h '+m+'m '+sc+'s';if(m>0)return m+'m '+sc+'s';return sc+'s';}
function sH(id,h){var e=document.getElementById(id);if(e)e.innerHTML=h;}
function sT(id,t){var e=document.getElementById(id);if(e)e.innerText=t;}

function onD(d){
  pa(dT,d.temperature);pa(dH,d.humidity);pa(dP,d.pressure);
  pa(dC,d.co2);pa(dN,d.nh3);pa(dP2,d.pm25);pa(dP1,d.pm10);
  pa(dI,d.iaq);pa(dA,d.aqi);

  sH('tVal',d.temperature.toFixed(1)+'<span class="cu"> \u00B0C</span>');
  sH('hVal',d.humidity.toFixed(1)+'<span class="cu"> %</span>');
  sH('pVal',d.pressure.toFixed(1)+'<span class="cu"> hPa</span>');
  sH('cVal',d.co2.toFixed(0)+'<span class="cu"> ppm</span>');
  sH('nVal',d.nh3.toFixed(2)+'<span class="cu"> ppm</span>');
  sH('p2Val',d.pm25.toFixed(0)+'<span class="cu"> \u03BCg/m\u00B3</span>');
  sH('p1Val',d.pm10.toFixed(0)+'<span class="cu"> \u03BCg/m\u00B3</span>');
  sT('aqV',d.aqi.toFixed(0));sT('iqV',d.iaq.toFixed(0));

  var ae=document.getElementById('aqC');if(ae){ae.innerText=d.aqi_cat;ae.className='cat '+getCat(d.aqi_cat);}
  var ie=document.getElementById('iqC');if(ie){ie.innerText=d.iaq_cat;ie.className='cat '+getCat(d.iaq_cat);}
  sT('uptD',fU(d.uptime));

  var bb=document.getElementById('bB');if(bb){bb.innerText='BME:'+(d.bme_ok?'OK':'ERR');bb.className='badge '+(d.bme_ok?'bok':'berr');}
  var pb=document.getElementById('pB');if(pb){pb.innerText='PMS:'+(d.pms_ok?'OK':'ERR');pb.className='badge '+(d.pms_ok?'bok':'berr');}
  var mb=document.getElementById('mB');if(mb){mb.innerText='MQ:'+(d.mq_ok?'OK':'ERR');mb.className='badge '+(d.mq_ok?'bok':'berr');}

  requestAnimationFrame(drawAll);
  if(!mP){mP=true;fetch('/mem').then(function(r){return r.text();}).then(function(t){sT('mem','Mem:'+t);mP=false;}).catch(function(){mP=false;});}
}

function TT(){isL=!isL;document.body.classList.toggle('light',isL);document.getElementById('tglBtn').classList.toggle('on',isL);}
function MR(){fetch('/refresh').catch(function(){});}
function DL(){window.location='/download';}
function CL(){if(confirm('Clear logs?'))fetch('/clear',{method:'POST'}).then(function(r){return r.text();}).then(function(t){alert(t);}).catch(function(){alert('Error');});}
</script>
</body>
</html>
)rawliteral";

void onEvent(AsyncWebSocket*s,AsyncWebSocketClient*c,AwsEventType t,void*a,uint8_t*d,size_t l){if(t==WS_EVT_CONNECT)c->text(buildJSON());}

void setupRoutes(){
  server.on("/",HTTP_GET,[](AsyncWebServerRequest*r){r->send_P(200,"text/html",index_html);});
  server.on("/download",HTTP_GET,[](AsyncWebServerRequest*r){if(LittleFS.exists(LOG_PATH)){auto*res=r->beginResponse(LittleFS,LOG_PATH,"text/csv");res->addHeader("Content-Disposition","attachment; filename=\"aqi_data.csv\"");r->send(res);}else r->send(404,"text/plain","No log");});
  server.on("/clear",HTTP_POST,[](AsyncWebServerRequest*r){if(LittleFS.exists(LOG_PATH))LittleFS.remove(LOG_PATH);ensureLogHeader();r->send(200,"text/plain","Cleared!");});
  server.on("/refresh",HTTP_GET,[](AsyncWebServerRequest*r){readSensorsOnce();notifyClients();r->send(200,"text/plain","OK");});
  server.on("/mem",HTTP_GET,[](AsyncWebServerRequest*r){r->send(200,"text/plain",String(ESP.getFreeHeap()/1024)+"KB/"+String(ESP.getHeapSize()/1024)+"KB");});
}

void setup(){
  Serial.begin(115200);delay(200);Serial.println("\n=== AQI v3.5 ===");
  Wire.begin(BME_SDA,BME_SCL);
  if(!LittleFS.begin(true))Serial.println("FS FAIL");else{Serial.println("FS OK");ensureLogHeader();}
  WiFi.mode(WIFI_AP);WiFi.softAP(AP_SSID,AP_PASS);delay(100);
  Serial.println("AP:"+String(AP_SSID)+" IP:"+WiFi.softAPIP().toString());
  pmsSerial.begin(9600,SERIAL_8N1,PMS_RX,PMS_TX);lastPMSSuccess=millis();lastPMSFlush=millis();
  if(!bme.begin(0x76)){if(!bme.begin(0x77))bmeOK=false;else bmeOK=true;}else bmeOK=true;
  if(bmeOK){bme.setTemperatureOversampling(BME680_OS_8X);bme.setHumidityOversampling(BME680_OS_2X);bme.setPressureOversampling(BME680_OS_4X);bme.setIIRFilterSize(BME680_FILTER_SIZE_3);bme.setGasHeater(320,150);Serial.println("BME OK");}
  MQ135.init();MQ135.setRegressionMethod(1);MQ135.setR0(MQ135_R0);MQ135.setA(110.3);MQ135.setB(-2.769);mqOK=true;
  ws.onEvent(onEvent);server.addHandler(&ws);setupRoutes();server.begin();
  Serial.println("Warmup...");unsigned long t0=millis();int wc=0;while(millis()-t0<10000){readSensorsOnce();wc++;delay(500);}
  buf_idx=0;buf_count=0;ema_initialized=true;Serial.println("Running");
}

void loop(){
  if(millis()-lastSensorRead>=SENSOR_INTERVAL){lastSensorRead=millis();readSensorsOnce();notifyClients();
    Serial.printf("T:%.1f H:%.1f CO2:%.0f NH3:%.2f PM25:%.0f IAQ:%.0f AQI:%.0f\n",temperature,humidity,co2_ppm,nh3_ppm,pm2_5,iaq,final_aqi);}
  if(millis()-lastLogWrite>=LOG_INTERVAL){lastLogWrite=millis();appendLog();}
  ws.cleanupClients(4);
}