#include "auditor.hpp"
#include "camera.hpp"
#include "storage.hpp"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

using namespace auditor;
namespace {
Settings settings;
Calibration calibration;
Cycle cycle;
bool clock_valid=false, camera_ready=false, storage_ready=false;
QueueHandle_t commands;
struct Command { char text[512]; };
static uint8_t gray[W*H];
int64_t mono() { return esp_timer_get_time()/1000; }
cJSON *event(const char *type) {
    auto *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"event",type);
    cJSON_AddNumberToObject(j,"ms",double(mono()));
    cJSON_AddNumberToObject(j,"epoch",clock_valid?double(time(nullptr)):0);
    return j;
}
void emit(cJSON *j) {
    char *text=cJSON_PrintUnformatted(j);
    if(text) { printf("%s\n",text); fflush(stdout); cJSON_free(text); }
    cJSON_Delete(j);
}
void reply(cJSON *request, bool ok, const char *message) {
    auto *j=event("reply"); auto *id=cJSON_GetObjectItemCaseSensitive(request,"id");
    if(cJSON_IsNumber(id)) cJSON_AddNumberToObject(j,"id",id->valuedouble);
    cJSON_AddBoolToObject(j,"ok",ok); cJSON_AddStringToObject(j,"message",message); emit(j);
}
void status() {
    auto *j=event("status");
    cJSON_AddStringToObject(j,"state",name(cycle.state));
    cJSON_AddBoolToObject(j,"acknowledged",cycle.acknowledged);
    cJSON_AddBoolToObject(j,"clock_valid",clock_valid);
    cJSON_AddBoolToObject(j,"camera_ready",camera_ready);
    cJSON_AddBoolToObject(j,"storage_ready",storage_ready);
    cJSON_AddBoolToObject(j,"calibrated",calibrated(calibration,settings));
    cJSON_AddBoolToObject(j,"has_present",calibration.has_present);
    cJSON_AddBoolToObject(j,"has_empty",calibration.has_empty);
    cJSON_AddNumberToObject(j,"heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"deadline_ms",double(cycle.deadline()));
    cJSON_AddNumberToObject(j,"due_ms",double(cycle.due()));
    auto *s=cJSON_AddObjectToObject(j,"settings");
    cJSON_AddNumberToObject(s,"profile",settings.profile);
    cJSON_AddNumberToObject(s,"hour",settings.hour); cJSON_AddNumberToObject(s,"minute",settings.minute);
    cJSON_AddNumberToObject(s,"window_s",settings.window_s); cJSON_AddNumberToObject(s,"interval_s",settings.interval_s);
    cJSON_AddNumberToObject(s,"confirmations",settings.confirmations);
    cJSON_AddNumberToObject(s,"max_distance",settings.max_distance); cJSON_AddNumberToObject(s,"margin",settings.margin);
    cJSON_AddNumberToObject(s,"context_distance",settings.context_distance); cJSON_AddNumberToObject(s,"stability",settings.stability);
    int roi[]={settings.roi.x,settings.roi.y,settings.roi.w,settings.roi.h};
    cJSON_AddItemToObject(s,"roi",cJSON_CreateIntArray(roi,4)); emit(j);
}
void report_transition(State old) {
    if(old==cycle.state) return;
    auto *j=event("transition"); cJSON_AddStringToObject(j,"from",name(old));
    cJSON_AddStringToObject(j,"state",name(cycle.state));
    cJSON_AddBoolToObject(j,"acknowledged",cycle.acknowledged); emit(j); status();
}
bool take(Sample &sample) {
    return camera_ready && camera_gray(gray) && extract(gray,sizeof(gray),settings.roi,sample);
}
Result observation() {
    int64_t start=mono(); Sample a,b; Result r;
    bool captured=take(a) && take(b);
    bool stable=captured && distance(a.roi.data(),b.roi.data(),N)<=settings.stability &&
        context_distance(a,b)<=settings.stability;
    if(stable) r=classify(b,calibration,settings);
    auto *j=event("observation");
    cJSON_AddStringToObject(j,"label",name(r.label));
    cJSON_AddBoolToObject(j,"captured",captured); cJSON_AddBoolToObject(j,"stable",stable);
    cJSON_AddNumberToObject(j,"distance_present",r.present); cJSON_AddNumberToObject(j,"distance_empty",r.empty);
    cJSON_AddNumberToObject(j,"distance_context",r.context);
    cJSON_AddNumberToObject(j,"duration_ms",double(mono()-start)); emit(j); return r;
}
bool collect(Sample &out) {
    Sample first,current;
    if(!take(first) || !first.quality) return false;
    uint32_t sums[N]{}, guard[GUARD]{};
    for(int k=0;k<5;++k) {
        if(!take(current) || !current.quality ||
           distance(current.roi.data(),first.roi.data(),N)>settings.stability ||
           context_distance(current,first)>settings.stability) return false;
        for(int i=0;i<N;++i) sums[i]+=current.roi[i];
        for(int i=0;i<GUARD;++i) guard[i]+=current.context[i];
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    for(int i=0;i<N;++i) out.roi[i]=uint8_t(sums[i]/5);
    for(int i=0;i<GUARD;++i) out.context[i]=uint8_t(guard[i]/5);
    out.context_count=first.context_count;
    out.quality=true; return true;
}
bool number(cJSON *j,const char *key,int &out) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key); if(!v) return true;
    if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<-100000 || v->valuedouble>100000 || std::floor(v->valuedouble)!=v->valuedouble) return false;
    out=int(v->valuedouble); return true;
}
bool number(cJSON *j,const char *key,float &out) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key); if(!v) return true;
    if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) return false;
    out=float(v->valuedouble); return true;
}
bool update(cJSON *j,Settings &s) {
    if(!number(j,"profile",s.profile) || !number(j,"hour",s.hour) || !number(j,"minute",s.minute) ||
       !number(j,"window_s",s.window_s) || !number(j,"interval_s",s.interval_s) || !number(j,"confirmations",s.confirmations) ||
       !number(j,"max_distance",s.max_distance) || !number(j,"margin",s.margin) ||
       !number(j,"context_distance",s.context_distance) || !number(j,"stability",s.stability)) return false;
    auto *roi=cJSON_GetObjectItemCaseSensitive(j,"roi");
    if(roi) {
        if(!cJSON_IsArray(roi) || cJSON_GetArraySize(roi)!=4) return false;
        int a[4];
        for(int i=0;i<4;++i) { auto *v=cJSON_GetArrayItem(roi,i);
            if(!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>W || std::floor(v->valuedouble)!=v->valuedouble) return false;
            a[i]=int(v->valuedouble);
        }
        s.roi={a[0],a[1],a[2],a[3]};
    }
    return valid(s);
}
void snapshot(cJSON *req) {
    if(!camera_ready || !camera_gray(gray)) { reply(req,false,"Falha de captura"); return; }
    size_t len=0;
    auto *encoded=static_cast<unsigned char*>(malloc(sizeof(gray)*4/3+8));
    if(!encoded) { reply(req,false,"Sem memoria"); return; }
    int rc=mbedtls_base64_encode(encoded,sizeof(gray)*4/3+8,&len,gray,sizeof(gray));
    if(rc==0) {
        // Base64 contains no JSON metacharacters; avoid two extra 25 KiB copies.
        printf("{\"event\":\"image\",\"ms\":%lld,\"epoch\":%lld,\"width\":%d,\"height\":%d,\"gray_base64\":\"%s\"}\n",
               static_cast<long long>(mono()),static_cast<long long>(clock_valid?time(nullptr):0),W,H,
               reinterpret_cast<char*>(encoded));
        fflush(stdout);
    }
    free(encoded); reply(req,rc==0,rc==0?"Imagem USB enviada":"Erro base64");
}
void handle(const char *text) {
    const char *end=nullptr;
    auto *j=cJSON_ParseWithOpts(text,&end,true);
    auto *cmd=cJSON_GetObjectItemCaseSensitive(j,"cmd");
    if(!cJSON_IsObject(j) || !cJSON_IsString(cmd)) { reply(j,false,"JSON invalido: use cmd"); cJSON_Delete(j); return; }
    const char *op=cmd->valuestring;
    if(!strcmp(op,"status")) { status(); reply(j,true,"Estado enviado"); }
    else if(!strcmp(op,"ack")) { cycle.acknowledge(); status(); reply(j,true,"Reconhecimento nao confirma retirada"); }
    else if(!strcmp(op,"cancel")) { cycle.cancel(); status(); reply(j,true,"Ciclo cancelado; reposicao e rearmamento necessarios"); }
    else if(cycle.active()) reply(j,false,"Ciclo ativo: apenas status, ack ou cancel");
    else if(!strcmp(op,"time")) {
        auto *v=cJSON_GetObjectItemCaseSensitive(j,"epoch");
        if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<1704067200 || v->valuedouble>4102444800 || std::floor(v->valuedouble)!=v->valuedouble)
            reply(j,false,"epoch deve ser inteiro Unix UTC entre 2024 e 2100");
        else { timeval tv{time_t(v->valuedouble),0}; clock_valid=settimeofday(&tv,nullptr)==0; reply(j,clock_valid,"Relogio ajustado; fuso fixo UTC-3"); }
    }
    else if(!strcmp(op,"set")) {
        Settings next=settings;
        if(!update(j,next)) reply(j,false,"Parametros invalidos");
        else {
            Calibration refs=calibration;
            bool changed=next.profile!=settings.profile || !same_roi(next.roi,settings.roi);
            if(changed) refs=Calibration{};
            esp_err_t e=storage_ready?save_settings(next,refs):ESP_ERR_INVALID_STATE;
            if(e!=ESP_OK) reply(j,false,"Falha NVS; alteracoes nao aplicadas");
            else {
                bool restart=next.profile!=settings.profile || !camera_ready;
                settings=next; calibration=refs; cycle.cancel();
                if(restart) camera_ready=camera_start(settings.profile)==ESP_OK;
                reply(j,true,camera_ready?"Salvo":"Salvo; camera indisponivel: confira perfil e cabo"); status();
            }
        }
    }
    else if(!strcmp(op,"calibrate")) {
        auto *label=cJSON_GetObjectItemCaseSensitive(j,"label");
        if(!cJSON_IsString(label) || (strcmp(label->valuestring,"present") && strcmp(label->valuestring,"empty"))) reply(j,false,"label: present ou empty");
        else {
            Calibration next=calibration; bool present=!strcmp(label->valuestring,"present");
            Sample sample;
            if(!collect(sample)) reply(j,false,"Cena instavel, escura, saturada ou camera indisponivel");
            else {
                if(present) { next.present=sample; next.has_present=true; }
                else { next.empty=sample; next.has_empty=true; }
                if(next.has_present && next.has_empty && !calibrated(next,settings)) reply(j,false,"Referencias pouco separadas ou contexto mudou; ajuste ROI e cena");
                else if(!storage_ready || save_settings(settings,next)!=ESP_OK) reply(j,false,"Falha ao salvar calibracao");
                else { calibration=next; cycle.cancel(); reply(j,true,"Referencia salva"); status(); }
            }
        }
    }
    else if(!strcmp(op,"clear_calibration")) {
        Calibration next{};
        if(!storage_ready || save_settings(settings,next)!=ESP_OK) reply(j,false,"Falha NVS");
        else { calibration=next; cycle.cancel(); reply(j,true,"Referencias removidas"); status(); }
    }
    else if(!strcmp(op,"snapshot")) snapshot(j);
    else if(!strcmp(op,"sample")) { observation(); reply(j,true,"Leitura registrada; nao arma ciclo"); }
    else if(!strcmp(op,"arm")) {
        int delay_s=0;
        if(!clock_valid || !camera_ready || !calibrated(calibration,settings)) reply(j,false,"Configure hora, camera e duas referencias antes de armar");
        else if(!number(j,"delay_s",delay_s) || delay_s<0 || delay_s>86400) reply(j,false,"delay_s entre 0 e 86400");
        else {
            Result r=observation();
            int64_t now=mono(), wait_s=delay_s;
            if(!delay_s) {
                int64_t local=int64_t(time(nullptr))-3*3600;
                int64_t seconds=local%86400;
                wait_s=settings.hour*3600+settings.minute*60-seconds;
                if(wait_s<=0) wait_s+=86400;
            }
            bool ok=cycle.arm(now,now+wait_s*1000,r.label,settings);
            reply(j,ok,ok?"Armado uma vez; nova dose exige rearmamento":"Dose presente nao confirmada"); status();
        }
    }
    else reply(j,false,"Comando desconhecido");
    cJSON_Delete(j);
}
void serial_task(void *) {
    Command cmd{}; size_t n=0; bool overflow=false;
    while(true) {
        uint8_t ch;
        if(uart_read_bytes(UART_NUM_0,&ch,1,pdMS_TO_TICKS(100))!=1) continue;
        if(ch=='\r') continue;
        if(ch=='\n') {
            if(overflow) snprintf(cmd.text,sizeof(cmd.text),"{\"cmd\":\"line_too_long\"}");
            else cmd.text[n]='\0';
            if(n || overflow) xQueueSend(commands,&cmd,portMAX_DELAY);
            n=0; overflow=false;
        } else if(n<sizeof(cmd.text)-1 && !overflow) cmd.text[n++]=char(ch);
        else overflow=true;
    }
}
} // namespace

extern "C" void app_main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    // Never erase NVS automatically: schema/full-storage errors remain visible.
    storage_ready=nvs_flash_init()==ESP_OK;
    esp_err_t stored=storage_ready?load_settings(settings,calibration):ESP_FAIL;
    if(stored!=ESP_OK) { settings=Settings{}; calibration=Calibration{}; }
    uart_config_t uart{};
    uart.baud_rate=115200; uart.data_bits=UART_DATA_8_BITS; uart.parity=UART_PARITY_DISABLE;
    uart.stop_bits=UART_STOP_BITS_1; uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE; uart.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0,&uart));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,2048,0,0,nullptr,0));
    commands=xQueueCreate(8,sizeof(Command));
    if(!commands || xTaskCreate(serial_task,"usb_commands",4096,nullptr,4,nullptr)!=pdPASS) abort();
    camera_ready=camera_start(settings.profile)==ESP_OK;
    auto *boot=event("boot"); cJSON_AddStringToObject(boot,"version","1.0.0");
    cJSON_AddStringToObject(boot,"storage_load",esp_err_to_name(stored));
    cJSON_AddStringToObject(boot,"message","Hora e rearmamento obrigatorios; som somente no painel USB"); emit(boot); status();
    int64_t heartbeat=mono();
    while(true) {
        State previous=cycle.state;
        int64_t now=mono();
        // If due, sample first only when still inside the observation window.
        if(cycle.needs_sample(now)) { Result r=observation(); cycle.observe(mono(),r.label); }
        cycle.tick(mono()); report_transition(previous);
        Command cmd;
        if(xQueueReceive(commands,&cmd,pdMS_TO_TICKS(20))==pdTRUE) handle(cmd.text);
        if(mono()-heartbeat>=10000) { status(); heartbeat=mono(); }
    }
}
