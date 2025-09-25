// ========================= src/tinyserver.c =========================
// Embedded version of your tiny_onboard_server.c

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdatomic.h>

#include "config.h"
#include "log.h"

static pthread_t g_thr;
static atomic_int g_run = 0;

// --- utils from your server ---
static void mask_secret(const char *in, char *out, size_t outsz){
    size_t n = in ? strlen(in) : 0;
    if (!in || !n){ snprintf(out,outsz,"<empty>"); return; }
    if (n <= 4){ snprintf(out,outsz,"****"); return; }
    snprintf(out,outsz,"%.*s****%.*s", 2, in, 2, in + (int)n - 2);
}

static const char* ci_strstr(const char *h, const char *n){
    if(!h||!n) return NULL; size_t L=strlen(n); if(!L) return h;
    for(const char *p=h; *p; ++p){
        size_t i=0;
        while(i<L && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)n[i])) i++;
        if(i==L) return p;
    }
    return NULL;
}

static int safe_write_file(const char *path, const char *content, mode_t mode){
    char tmp[256];
    snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"w"); if(!f){ return 0; }
    if(fputs(content,f)==EOF){ fclose(f); unlink(tmp); return 0; }
    if(fclose(f)!=0){ unlink(tmp); return 0; }
    if(chmod(tmp,mode)!=0){ unlink(tmp); return 0; }
    if(rename(tmp,path)!=0){ unlink(tmp); return 0; }
    return 1;
}

/* tiny JSON string extractor: finds "key":"value" (no full JSON parsing) */
static int json_get_str(const char *json, const char *key, char *out, size_t outsz){
    if (!json || !key || !out || outsz < 2) return 0;
    char pattern[128];
    snprintf(pattern,sizeof(pattern),"\"%s\"",key);
    const char *k = strstr(json, pattern); if(!k) return 0;
    const char *p = strchr(k + strlen(pattern), ':'); if(!p) return 0;
    for (p++; *p && isspace((unsigned char)*p); p++);
    if (*p != '\"') return 0;
    p++; // after opening quote
    size_t i=0;
    while(*p && *p!='\"' && i<outsz-1){
        if (*p == '\\' && *(p+1)) p++; // skip backslash, copy next char raw
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

static void json_quote_or_null(char *out, size_t outsz, const char *str){
    if(!str||!str[0]){ snprintf(out,outsz,"null"); return; }
    char buf[256]; size_t j=0;
    for(size_t i=0; str[i] && j<sizeof(buf)-2; i++){
        char c=str[i];
        if(c=='\"'||c=='\\'){ if(j<sizeof(buf)-2) buf[j++]='\\'; }
        buf[j++]=c;
    }
    buf[j]='\0';
    snprintf(out,outsz,"\"%s\"",buf);
}

static int get_iface_ip(char ipbuf[64]){
    int fd = socket(AF_INET, SOCK_DGRAM, 0); if(fd<0) return 0;
    struct ifreq ifr; memset(&ifr,0,sizeof(ifr));
    strncpy(ifr.ifr_name, IFACE, IFNAMSIZ-1);
    if(ioctl(fd,SIOCGIFADDR,&ifr)==0){
        struct sockaddr_in *sin=(struct sockaddr_in *)&ifr.ifr_addr;
        const char *ip=inet_ntoa(sin->sin_addr);
        if(ip && strcmp(ip,"0.0.0.0")!=0){
            strncpy(ipbuf,ip,63); ipbuf[63]='\0';
            close(fd); return 1;
        }
    }
    close(fd); return 0;
}

static const char* detect_mode(char ip_out[64]){
    ip_out[0]='\0';
    if (access("/var/run/hostapd/hostapd.pid", F_OK) == 0) { (void)get_iface_ip(ip_out); return "AP"; }
    if (get_iface_ip(ip_out)) {
        return strncmp(ip_out, AP_SUBNET_PREFIX, strlen(AP_SUBNET_PREFIX))==0 ? "AP" : "STA";
    }
    return "UNKNOWN";
}

static int persist_all(const char *ssid, const char *wifi_password,
                       const char *username, const char *user_password,
                       const char *camera_id){
    system("mkdir -p /system/etc >/dev/null 2>&1");
    char txt[1024];
    snprintf(txt,sizeof(txt),
        "ssid=%s\n"
        "wifi_psk=%s\n"
        "username=%s\n"
        "password=%s\n"
        "camera_id=%s\n"
        "is_reg=1\n"
        "sensor=imx327\n"
        "vendor=teton\n",
        ssid,wifi_password,username,user_password,camera_id);
    if(!safe_write_file(CFG_TXT_PATH, txt, 0600)) return 0;
#if ENABLE_WPA_WRITE
    char wpa[1024];
    snprintf(wpa,sizeof(wpa),
        "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
        "update_config=1\n"
        "country=BD\n"
        "\n"
        "network={\n"
        "    ssid=\"%s\"\n"
        "    psk=\"%s\"\n"
        "    key_mgmt=WPA-PSK\n"
        "}\n",
        ssid,wifi_password);
    if(!safe_write_file(WPA_PATH, wpa, 0600)) return 0;
#endif
    return 1;
}

static void send_json(int client, int code, const char *json){
    char header[256];
    snprintf(header,sizeof(header),
        "HTTP/1.1 %d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        code, json?strlen(json):0);
    send(client, header, strlen(header), 0);
    if(json) send(client, json, strlen(json), 0);
}

static void handle_onboard(int client, const char *body){
    char ssid[256]="", wifi_password[256]="", username[256]="", user_password[256]="", camera_id[256]="";
    if (!json_get_str(body, "ssid", ssid, sizeof(ssid)) ||
        !json_get_str(body, "wifi_password", wifi_password, sizeof(wifi_password))) {
        send_json(client, 400, "{\"ok\":false,\"error\":\"missing ssid or wifi_password\"}\n");
        return;
    }
    json_get_str(body, "username",  username,      sizeof(username));
    json_get_str(body, "password",  user_password, sizeof(user_password));
    json_get_str(body, "camera_id", camera_id,     sizeof(camera_id));

    if (!persist_all(ssid, wifi_password, username, user_password, camera_id)){
        send_json(client, 500, "{\"ok\":false,\"error\":\"persist failed\"}\n");
        return;
    }

    send_json(client, 200, "{\"ok\":true,\"msg\":\"saved; switching to STA\"}\n");

    pid_t pid=fork();
    if(pid==0){
        execl("/bin/sh","sh", WIFI_SH, (char*)NULL);
        _exit(0);
    }
}

static void load_cfg(char *ssid, char *username, char *camera_id,
                     int *has_wifi_psk, int *has_user_pass){
    FILE *f=fopen(CFG_TXT_PATH,"r");
    ssid[0]=username[0]=camera_id[0]='\0';
    if(has_wifi_psk) *has_wifi_psk=0;
    if(has_user_pass) *has_user_pass=0;
    if(!f) return;

    char line[256];
    while(fgets(line,sizeof(line),f)){
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq='\0';
        char *k=line, *v=eq+1;
        v[strcspn(v,"\r\n")]='\0';
        if(strcmp(k,"ssid")==0) strncpy(ssid,v,255), ssid[255]='\0';
        else if(strcmp(k,"username")==0) strncpy(username,v,255), username[255]='\0';
        else if(strcmp(k,"camera_id")==0) strncpy(camera_id,v,255), camera_id[255]='\0';
        else if(strcmp(k,"wifi_psk")==0 && has_wifi_psk) *has_wifi_psk=(v[0]!='\0');
        else if(strcmp(k,"password")==0 && has_user_pass) *has_user_pass=(v[0]!='\0');
    }
    fclose(f);
}

static void handle_status(int client){
    char ip[64]; const char *mode=detect_mode(ip);
    char ssid[256], username[256], camera_id[256]; int has_wifi_psk=0, has_user_pass=0;
    load_cfg(ssid, username, camera_id, &has_wifi_psk, &has_user_pass);
    char ipq[68], ssidq[260], userq[260], camidq[260];
    json_quote_or_null(ipq,sizeof(ipq), ip[0]?ip:NULL);
    json_quote_or_null(ssidq,sizeof(ssidq), ssid[0]?ssid:NULL);
    json_quote_or_null(userq,sizeof(userq), username[0]?username:NULL);
    json_quote_or_null(camidq,sizeof(camidq), camera_id[0]?camera_id:NULL);
    char json[1024];
    snprintf(json,sizeof(json),
        "{\"ok\":true,"
         "\"mode\":\"%s\","
         "\"ip\":%s,"
         "\"config\":{"
           "\"ssid\":%s,"
           "\"username\":%s,"
           "\"camera_id\":%s,"
           "\"has_wifi_psk\":%s,"
           "\"has_user_password\":%s"
         "}"
        "}\n",
        mode, ipq, ssidq, userq, camidq,
        has_wifi_psk?"true":"false",
        has_user_pass?"true":"false");
    send_json(client,200,json);
}

static void* server_thread(void *arg){
    atomic_int *started = (atomic_int*)arg;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){ perror("socket"); *started = -1; return NULL; }
    int one=1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in serv; memset(&serv,0,sizeof(serv));
    serv.sin_family=AF_INET; serv.sin_addr.s_addr=INADDR_ANY; serv.sin_port=htons(TINY_PORT);
    if (bind(sockfd,(struct sockaddr*)&serv,sizeof(serv))<0){ perror("bind"); *started=-1; close(sockfd); return NULL; }
    if (listen(sockfd,8)<0){ perror("listen"); *started=-1; close(sockfd); return NULL; }

    *started = 1; logf_tag("tiny","listening on :%d", TINY_PORT);
    g_run = 1;

    while (g_run){
        struct sockaddr_in cli; socklen_t clilen=sizeof(cli);
        int cfd = accept(sockfd,(struct sockaddr*)&cli,&clilen);
        if (cfd<0){ if(errno==EINTR) continue; break; }

        char req[163840]; int n = recv(cfd, req, sizeof(req)-1, 0);
        if (n<=0){ close(cfd); continue; }
        req[n]='\0';

        int is_post_onboard=0, is_get_status=0;
        if (strncmp(req, "POST /onboard ", 14) == 0 || strstr(req, "POST /onboard ")) is_post_onboard=1;
        else if (strncmp(req, "GET /status ", 12) == 0 || strstr(req, "GET /status ")) is_get_status=1;

        if (!is_post_onboard && !is_get_status){
            send_json(cfd,404,"{\"ok\":false,\"error\":\"not found\"}\n");
            close(cfd); continue;
        }

        if (is_get_status){
            handle_status(cfd);
            close(cfd);
            while (waitpid(-1,NULL,WNOHANG)>0) {}
            continue;
        }

        int content_len=0; const char *cl = ci_strstr(req, "Content-Length:");
        if (cl){
            cl += strlen("Content-Length:");
            while(*cl && (*cl==':'||isspace((unsigned char)*cl))) cl++;
            content_len = atoi(cl);
        }
        if (content_len<=0 || content_len>=163840){
            send_json(cfd,400,"{\"ok\":false,\"error\":\"invalid Content-Length\"}\n");
            close(cfd); continue;
        }

        const char *sep = strstr(req, "\r\n\r\n");
        char body[163840]; int body_read=0;
        if (sep){
            sep+=4;
            int already = (int)((req+n)-sep);
            if(already>0){
                if(already>content_len) already=content_len;
                memcpy(body,sep,already);
                body_read=already;
            }
        }
        while (body_read<content_len){
            int r = recv(cfd, body+body_read, content_len-body_read, 0);
            if(r<=0) break;
            body_read+=r;
            if(body_read>=163839) break;
        }
        body[(body_read<163839)?body_read:163839]='\0';

        handle_onboard(cfd, body);
        close(cfd);
        while (waitpid(-1,NULL,WNOHANG)>0) {}
    }

    close(sockfd);
    *started = 0;
    return NULL;
}

int tiny_server_start(atomic_int *server_started){
    *server_started = 0;
    if (pthread_create(&g_thr,NULL,server_thread,server_started)!=0){ return -1; }
    // caller may poll *server_started to become 1
    return 0;
}

void tiny_server_stop(void){
    if (g_run){ g_run = 0; }
    // poke the accept() with a dummy connection attempt
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s>=0){
        struct sockaddr_in a; memset(&a,0,sizeof(a));
        a.sin_family=AF_INET; a.sin_port=htons(TINY_PORT); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        (void)connect(s,(struct sockaddr*)&a,sizeof(a));
        close(s);
    }
    pthread_join(g_thr,NULL);
}
