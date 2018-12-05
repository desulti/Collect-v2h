#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "mqtt_client.h"
#include "DHT22.h"
#include "cJSON.h"

#include "lwip/err.h"
#include "lwip/apps/sntp.h"

#define WIFI_SSID      "NoOne" //"NCT"
#define WIFI_PASSWORD  "tumotdentam" //"dccndccn"

#define MQTT_URI       "mqtt://test.mosquitto.org:1883"

#define AUTHENTICATION    "{\"id\":%s,"\
                           "\"password\":\"nct_laboratory\","\
                           "\"device_type\":\"collect\"}"

#define DATA_SENSOR       "{\"id\":%s,"\
                           "\"temperature\":%.1f,"\
                           "\"humidity\":%.1f,"\
                           "\"TDS\":900,"\
                           "\"pH\":%.1f,"\
                           "\"time_harvest\":100}"

#define NOTIFICATION      "%s has been turned %s!"
						   
#define TOPIC_AUTHEN            "nct_authentication"
#define TOPIC_AUTHEN_RESULT     "nct_authentication_result_%s"
#define TOPIC_COLLECT           "/test/qos0"
#define TOPIC_COLLECT_SERVER    "nct_collect_%s"
#define TOPIC_KEEP_ALIVE        "nct_keep_alive"
#define TOPIC_COMMAND_SERVER	"nct_command_%s"
#define TOPIC_ERROR             "nct_error"

#define MAXIMUM_RETRY_AUTHENTICATE  2

#define ONE_SECOND_SLEEP  1000000
#define ONE_MINUTE_SLEEP 1000000*60
#define ONE_MINUTE_DELAY 1000*60

#define PH_SENSOR_PIN          (ADC1_CHANNEL_4) // read pH, GPIO32
#define TEMP_HUMI_SENSOR_PIN   4                // read temp and humi

#define GPIO_OUTPUT_LAMP_01 "unknown"
#define GPIO_OUTPUT_LAMP_02 "unknown"
#define GPIO_OUTPUT_LAMP_03 "unknown"
#define GPIO_OUTPUT_LAMP_04 "unknown"
//Dia chi GPIO Output de dieu khien cac den - Kiet
#define GPIO_OUTPUT_PUMP_01 "unknown"
#define GPIO_OUTPUT_PUMP_02 "unknown"
#define GPIO_OUTPUT_PUMP_03 "unknown"
#define GPIO_OUTPUT_PUMP_04 "unknown"
//Dia chi GPIO Output de dieu khien cac bom - Kiet
/* #define GPIO_OUTPUT_PIN_SEL ((1ULL<<GPIO_OUTPUT_LAMP_01) | (1ULL<<GPIO_OUTPUT_LAMP_02) | (1ULL<<GPIO_OUTPUT_LAMP_03) | (1ULL<<GPIO_OUTPUT_LAMP_04)\
							| (1ULL<<GPIO_OUTPUT_PUMP_01) | (1ULL<<GPIO_OUTPUT_PUMP_02) | (1ULL<<GPIO_OUTPUT_PUMP_03) | (1ULL<<GPIO_OUTPUT_PUMP_04))
 */
static const char *TAG          = "ESP32-COLLECT";
static const char *DEVICE_ID    = "12";
static const char *PASSWORD		= "nct_laboratory";
static const char *DEVICE_TYPE	= "collect";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static int msg_id_subscribed_authen_result = -1;
static int msg_id_subscribed_command = -1;
static int msg_id_published_authen = -1;
static int msg_id_published_collect = -1;
//static bool isDataSent = false;

static esp_mqtt_client_handle_t client;

//RTC_DATA_ATTR bool isAuthenticated = false;   //Bien luu trang thai esp32_collect da duoc xac thuc hay chua (luu ca khi deep sleep)
RTC_NOINIT_ATTR bool isAuthenticated;   //Bien luu trang thai esp32_collect da duoc xac thuc hay chua (luu ca khi restart) - Kiet
//RTC_DATA_ATTR float time_sleep = 0;           //Bien luu thoi gian ngu cua esp32_collect
RTC_NOINIT_ATTR float time_sleep;           //Bien luu thoi gian restart cua esp32_collect
//RTC_DATA_ATTR float published_sensors_data_count = 0;
RTC_NOINIT_ATTR float published_sensors_data_count;
//RTC_DATA_ATTR int key = 0;
RTC_NOINIT_ATTR int key;

static int count_authenticated_error = 0;     //Bien dem so lan xac thuc khong thanh cong

static float phVal;
static float tempVal;
static float humiVal;
//static float tdsVal;

//static void restart_wifi_and_mqtt_client(esp_mqtt_client_handle_t client);
//Ham khoi lai wifi va mqtt client - Kiet
static void obtain_time(void);
static void initialize_sntp(void);
//Ham lay thoi gian va khoi tao giao thuc sntp - Kiet
static time_t now;
//Bien luu thoi gian hien tai - Kiet
static void change_device_state(char* device_name, char* command);
int publish_sensor_data_to_broker(esp_mqtt_client_handle_t client);

/* static bool lamp_01_state = 0;
static bool lamp_02_state = 0;
static bool lamp_03_state = 0;
static bool lamp_04_state = 0;
//Trang thai cua cac den
static bool pump_01_state = 0;
static bool pump_02_state = 0;
static bool pump_03_state = 0;
static bool pump_04_state = 0;
//Trang thai cua cac bom */

/*
Ham doc du lieu sensor
*/
float get_pH() { // cần thay đổi và điều chỉnh thêm
    unsigned long int avgValue;
    uint32_t buf[10], temp;

    for (int i = 0; i < 10; i++) {
        buf[i] = adc1_get_raw(PH_SENSOR_PIN); // dải điện thế đọc của chân analog từ 0-->1.1(V)
        //printf("%d\n", buf[i]);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (buf[i] > buf[j]) {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }
    avgValue = 0;
    for (int i = 2; i < 8; i++) {
        avgValue += buf[i];
    }
    avgValue = avgValue / 6;
    float voltageAvgValue = (float)avgValue * 5.0 / 1024;
    float phValue = -5.70 * voltageAvgValue + 21.34;
    return phValue;
}

/*
Ham xu ly cua tac vu doc du lieu tu cac sensor
Dang du dung DHT (temperature, humidity module)
Can viet lai doc pH sensor, them doc TDS sensor
*/
void read_sensors_data_task(){
    for(;;)
    {
        printf("\nRead data from sensors ...\n");
        readDHT(); //Doc du lieu tu DHT module
        tempVal = getTemperature();
        humiVal = getHumidity();
        phVal = get_pH();
        printf("Temperature: %.1f, Humidity: %.1f, pH: %.1f\n", tempVal, humiVal, phVal);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
/*
Ham xu ly tac vu cap nhat trang thai cac den va cac may bom
*/
/* void update_lamps_and_pumps_state_task(){
	//config_gpio phan nay van chua nam chac - Kiet
	for(;;)
	{
		gpio_set_level(GPIO_OUTPUT_LAMP_01, lamp_01_state);
		gpio_set_level(GPIO_OUTPUT_LAMP_02, lamp_02_state);
		gpio_set_level(GPIO_OUTPUT_LAMP_03, lamp_03_state);
		gpio_set_level(GPIO_OUTPUT_LAMP_04, lamp_04_state);
		//Cap nhat trang thai cho cac den - Kiet
		gpio_set_level(GPIO_OUTPUT_PUMP_01, pump_01_state);
		gpio_set_level(GPIO_OUTPUT_PUMP_02, pump_02_state);
		gpio_set_level(GPIO_OUTPUT_PUMP_03, pump_03_state);
		gpio_set_level(GPIO_OUTPUT_PUMP_04, pump_04_state);
		//Cap nhat trang thai cho cac may bom - Kiet
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
} */
/*
Ham xu ly tac vu gui du lieu sau mot thoi gian nhat dinh - Kiet
*/
void do_publish_sensor_data_task(){
	for(;;)
	{
		//printf("Task Stack High Water Mark: %d\n", uxTaskGetStackHighWaterMark(NULL));
		msg_id_published_collect = publish_sensor_data_to_broker(client);
		vTaskDelay(time_sleep * ONE_MINUTE_DELAY / portTICK_PERIOD_MS);
	}
}
/*
Ham gui yeu cau xac thuc (nct_authentication) den mqtt broker 
Tra ve msg_id cua published topic
*/
int publish_topic_authentication(esp_mqtt_client_handle_t client) {
    /* int authen_token_len = strlen(DEVICE_ID) + strlen(AUTHENTICATION);
    char authen_token[authen_token_len];
    sprintf(authen_token, AUTHENTICATION, DEVICE_ID); //Fill du lieu vao authen_token */
	//Tao file json de gui yeu cau xac thuc - Kiet
	cJSON *root;
	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "id", cJSON_CreateString(DEVICE_ID));
	cJSON_AddItemToObject(root, "password", cJSON_CreateString(PASSWORD));
	cJSON_AddItemToObject(root, "device_type", cJSON_CreateString(DEVICE_TYPE));
	char *json;
	json = cJSON_Print(root);
	cJSON_Delete(root);
    int qos = 2;
    int msg_id = -1;
    msg_id = esp_mqtt_client_publish(client, TOPIC_AUTHEN, json, 0, qos, 0);
    return msg_id;
}

/*
Ham lang nghe tra loi xac thuc (nct_authentication_result_<DEVICE_ID>) tu mqtt broker tra loi
Tra ve msg_id cua subcribed topic
*/
int subscribe_topic_authentication_result(esp_mqtt_client_handle_t client) {
    int qos = 2;
    int msg_id = -1;
    int topic_authen_result_len = strlen(DEVICE_ID) + strlen(TOPIC_AUTHEN_RESULT);
    char topic_authen_result[topic_authen_result_len];
    sprintf(topic_authen_result, TOPIC_AUTHEN_RESULT, DEVICE_ID);
    msg_id = esp_mqtt_client_subscribe(client, topic_authen_result, qos);
    return msg_id;
}

/*
Ham lang nghe menh lenh (nct_command_<DEVICE_ID>) tu mqtt broker gui den
Tra ve msg_id cua subcribed topic
*/
int subscribe_topic_command(esp_mqtt_client_handle_t client) {
    int qos = 2;
    int msg_id = -1;
    int topic_command_len = strlen(DEVICE_ID) + strlen(TOPIC_COMMAND_SERVER);
    char topic_command[topic_command_len];
    sprintf(topic_command, TOPIC_COMMAND_SERVER, DEVICE_ID);
    msg_id = esp_mqtt_client_subscribe(client, topic_command, qos);
	printf("Subcribe to %s sent", topic_command);
    return msg_id;
}

/*
Ham gui goi tin chua sensors data toi mqtt broker
Tra ve msg_id cua published topic
*/
int publish_sensor_data_to_broker(esp_mqtt_client_handle_t client) {
	//Sua lai ham gui data de dong goi file json - Kiet
    //char data_buf[128];
    int qos = 2;
    int msg_id = -1;
    //sprintf(data_buf, DATA_SENSOR, DEVICE_ID, tempVal, humiVal, phVal);
	cJSON *root, *sensorsdata, *sensordata_01, *sensordata_02, *sensordata_03;
	ESP_LOGI(TAG, "object declared successful");
	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "message_id", cJSON_CreateString("<message_id>"));
	
	//Lay thoi gian o thoi diem hien tai
	char strftime_buf[64];
	static struct tm timeinfo;
	time(&now);
    localtime_r(&now, &timeinfo);
	// Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2018 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }
	setenv("TZ", "ICT-7", 1);
	tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	cJSON_AddItemToObject(root, "timestamp", cJSON_CreateString(strftime_buf));
	cJSON_AddNumberToObject(root, "farm_id", 	1);
	cJSON_AddItemToObject(root, "device_id", cJSON_CreateString(DEVICE_ID));
	cJSON_AddNumberToObject(root, "key",		key);
	ESP_LOGI(TAG, "add items to object successful");
	cJSON_AddItemToObject(root, "sensors_data", sensorsdata=cJSON_CreateArray());
	ESP_LOGI(TAG, "add array to object successful");
	sensordata_01 = cJSON_CreateObject();
	cJSON_AddNumberToObject(sensordata_01,"sensor_id",		1);
	cJSON_AddStringToObject(sensordata_01,"sensor_name",		"tempSensor??"); //Nho sua
	cJSON_AddNumberToObject(sensordata_01,"sensor_value",		tempVal);
	cJSON_AddItemToArray(sensorsdata, sensordata_01);
	sensordata_02 = cJSON_CreateObject();
	cJSON_AddNumberToObject(sensordata_02,"sensor_id",		2);
	cJSON_AddStringToObject(sensordata_02,"sensor_name",		"humiSensor??"); //Nho sua
	cJSON_AddNumberToObject(sensordata_02,"sensor_value",		humiVal);
	cJSON_AddItemToArray(sensorsdata, sensordata_02);
	sensordata_03 = cJSON_CreateObject();
	cJSON_AddNumberToObject(sensordata_03,"sensor_id",		3);
	cJSON_AddStringToObject(sensordata_03,"sensor_name",		"pHSensor??"); //Nho sua
	cJSON_AddNumberToObject(sensordata_03,"sensor_value",		phVal);
	cJSON_AddItemToArray(sensorsdata, sensordata_03);
	ESP_LOGI(TAG, "build json successful");
	//Dong goi xong file json
	char *json;
	json = cJSON_Print(root);
	cJSON_Delete(root);
    //msg_id = esp_mqtt_client_publish(client, TOPIC_COLLECT, json, 0, qos, 0);
	// Gio se gui file vao topic TOPIC_COLLECT_SERVER = topic_collect_id
	int topic_collect_server_len = strlen(DEVICE_ID) + strlen(TOPIC_COLLECT_SERVER);
    char topic_collect_server[topic_collect_server_len];
    sprintf(topic_collect_server, TOPIC_COLLECT_SERVER, DEVICE_ID);
	msg_id = esp_mqtt_client_publish(client, topic_collect_server, json, 0, qos, 0);
	ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
	//Hoan thanh gui file - Kiet
    printf("\nPublish: %s\n", json);
    published_sensors_data_count++;
    printf("Published sensors data count: %.1f\n\n", published_sensors_data_count);
    return msg_id;
}

/*
*/
bool is_topic_authentication_result(char *topic) {
    int topic_authen_result_len = strlen(DEVICE_ID) + strlen(TOPIC_AUTHEN_RESULT);
    char topic_authen_result[topic_authen_result_len];
    sprintf(topic_authen_result, TOPIC_AUTHEN_RESULT, DEVICE_ID);

    if(strcmp(topic_authen_result, topic) == 0)
        return true;
    else return false;
}

/*
*/
bool is_topic_command(char *topic) {
    int topic_command_len = strlen(DEVICE_ID) + strlen(TOPIC_COMMAND_SERVER);
    char topic_command[topic_command_len];
    sprintf(topic_command, TOPIC_COMMAND_SERVER, DEVICE_ID);

    if(strcmp(topic_command, topic) == 0)
        return true;
    else return false;
}

/* Ham event_handle cho cac su kien cua mqtt */
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            if(!isAuthenticated) { 
                //Neu thiet bi esp2_collect chua duoc xac thuc thi gui yeu cau xac thuc toi broker
                msg_id_published_authen = publish_topic_authentication(client);
                //Lang nghe tra loi xac thuc
				msg_id_subscribed_command = subscribe_topic_command(client);
                msg_id_subscribed_authen_result = subscribe_topic_authentication_result(client);
            } else {
                //Neu thiet bi esp32_collect da duoc xac thuc thi gui sensors data toi broker
				msg_id_subscribed_command = subscribe_topic_command(client);
                msg_id_published_collect = publish_sensor_data_to_broker(client);
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
			esp_restart();
			//Neu khong ket noi duoc voi mqtt server khoi dong lai wifi va mqtt client - Kiet
			//restart_wifi_and_mqtt_client(client);
            break;
        }

        case MQTT_EVENT_SUBSCRIBED: {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            if(msg_id_subscribed_authen_result != -1 && msg_id_subscribed_authen_result == event->msg_id) {
                ESP_LOGI(TAG, "sent subscribe authentication_result successful, msg_id=%d", event->msg_id);
                msg_id_subscribed_authen_result = -1;
            }
			else if(msg_id_subscribed_command != -1 && msg_id_subscribed_command == event->msg_id) {
                ESP_LOGI(TAG, "sent subscribe command_server successful, msg_id=%d", event->msg_id);
                msg_id_subscribed_command = -1;
            }
            break;
        }

        case MQTT_EVENT_UNSUBSCRIBED: {
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        }

        case MQTT_EVENT_PUBLISHED: {
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            //Truong hop gui yeu cau xac thuc
            if(msg_id_published_authen != -1 && msg_id_published_authen == event->msg_id) {
                ESP_LOGI(TAG, "sent publish authentication successful, msg_id=%d", event->msg_id);
                msg_id_published_authen = -1;
            }
            //Truong hop gui goi du lieu sensors
            if(msg_id_published_collect != -1 && msg_id_published_collect == event->msg_id) {
                ESP_LOGI(TAG, "sent publish sensors data successful, msg_id=%d", event->msg_id);
                msg_id_published_collect = -1;
                //Gui xong di ngu (deep sleep: Che do nay khi wake up se chay lai app_main())
                //printf("\nGO TO DEEP SLEEP in : %g minutes\n", time_sleep);
				printf("\nPUBLISH AGAIN in : %g minutes\n", time_sleep);
				//isDataSent = true;
                //esp_deep_sleep(ONE_MINUTE_SLEEP * time_sleep);
            }
            break; 
        }

        case MQTT_EVENT_DATA: {//co du lieu den
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            //Lay topic va data duoc gui den
            char received_topic[event->topic_len + 1];
            //char received_data[event->data_len + 1];
            sprintf(received_topic, "%.*s", event->topic_len, event->topic);
            //sprintf(received_data, "%.*s", event->data_len, event->data);
            
            if(is_topic_authentication_result(received_topic)) {//Neu la topic tra loi xac thuc (nct_authentication_result_<DEVICE_ID>)
                
                /* char *token;
                char result[5];
                char time[4];
                int count = 0;
                token = strtok(received_data, "_"); //Split string rev by character "_"
                                           //Receive "PASS" and colletion time
                while (count < 2) {
                    if (count == 0) {
                        strcpy(result, token);
                    }
                    if (count == 1) {
                        strcpy(time, token);
                    }
                    printf("%s\n", token);
                    token = strtok(NULL, "_");
                    count++;
                } */
				// Su dung ding dang json de mo goi tin - Kiet
				cJSON *root = cJSON_Parse(event->data);
				cJSON *result = cJSON_GetObjectItem(root,"result");

				// Kiem tra goi tin co dung cu phap hay khong - Kiet
				if ((!cJSON_HasObjectItem(root, "result"))
					|| ((strcmp(result->valuestring, "PASS")) != 0)) {
					if (!cJSON_HasObjectItem(root, "result"))
						printf("Result not found! Please check your server json format again!\n");
					//XAC THUC THAT BAI
                    printf("AUTHENTICATION FAILED!\n");
                    if (count_authenticated_error > MAXIMUM_RETRY_AUTHENTICATE) {
                        // TO_DO
                        printf("PUBLISH TO NCT_ERROR!\n");
						esp_restart();
						//restart_wifi_and_mqtt_client(client);
                        //thu lai khong duoc thi khoi dong lai esp32_collect
                        //...
                    } else {//Thu gui xac thuc lai
                        count_authenticated_error += 1;
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        printf("RE-AUTHENTICATION!\n");
                        msg_id_published_authen = publish_topic_authentication(client);
                    }
				}
				//printf("Result: %s\n", result->valuestring);
                else { //XAC THUC THANH CONG
                    printf("AUTHENTICATION SUCCESS!\n");
                    isAuthenticated = true;
					if (!cJSON_HasObjectItem(root,"cycle"))
						printf("Cycle not found! Please check your server json format again!\n");
					else
					{
						char *time = cJSON_Print(cJSON_GetObjectItem(root,"cycle"));
                    	time_sleep = atof(time);
					}
					if (!cJSON_HasObjectItem(root,"key"))
						printf("Key not found! Please check your server json format again!\n");
					else
					{
						cJSON *key_Obj = cJSON_GetObjectItem(root,"key");
						key = strtol(key_Obj->valuestring, NULL, 0);
					}
					//printf("Key: %d\n", key);
                    //Xac thuc thanh cong thi gui sensors data toi broker 
                    //msg_id_published_collect = publish_sensor_data_to_broker(client);
					xTaskCreate(&do_publish_sensor_data_task, "do_publish_sensor_data_task", 2400, NULL, 5, NULL);
                }
            }
            else if (is_topic_command(received_topic)) { //Thuc hien menh lenh neu topic la nct_command_id - Kiet
				cJSON *root = cJSON_Parse(event->data);
				if (!cJSON_HasObjectItem(root, "device_name"))
					printf("Device not found! Please check your server json format again!\n");
				else
				{
					cJSON *device_name = cJSON_GetObjectItem(root,"device_name");
					if (!cJSON_HasObjectItem(root, "command"))
						printf("Command not found! Please check your server json format again!\n");
					else
					{
						cJSON *command = cJSON_GetObjectItem(root,"command");
						change_device_state(device_name->valuestring, command->valuestring);
					}
				}
			}
			break;
        }

        case MQTT_EVENT_ERROR: {
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        }
    }
    return ESP_OK;
}

static esp_mqtt_client_handle_t mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = MQTT_URI,
        .client_id = "esp32-collect",
       /*  .username = "user",
        .password = "pass", */
        .lwt_topic = "/lwt",
        .lwt_msg = "offline",
        .lwt_qos = 0,
        .lwt_retain = 0,
        .event_handle = mqtt_event_handler
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
    return client;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            //Ket noi wifi thanh cong, nhan duoc IP => khoi dong mqtt 
            client = mqtt_app_start();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void) {
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

/* static void restart_wifi_and_mqtt_client(esp_mqtt_client_handle_t client)
{
	esp_mqtt_client_stop(client);
	ESP_LOGI(TAG, "Client stopped");
	ESP_ERROR_CHECK(esp_wifi_stop());
	// Tat mqtt client va wifi va doi mot lat - Kiet
	vTaskDelay(10 / portTICK_PERIOD_MS);
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "Waiting for wifi");
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	// Noi lai wifi va khoi dong lai client - Kiet
	esp_mqtt_client_start(client);
	ESP_LOGI(TAG, "Client restarted!");
} */

static void obtain_time(void)
{
    /* ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY); */
    initialize_sntp();

    // wait for time to be set
    now = 0;
    static struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while(timeinfo.tm_year < (2018 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    /* ESP_ERROR_CHECK( esp_wifi_stop() ); */
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void change_device_state(char* device_name, char* command)
{
	/* gpio_num_t target_device_gpio_num; */
	/* if (strcmp(device_name, "LAMP_01") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_LAMP_01;
	}
	else if (strcmp(device_name, "LAMP_02") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_LAMP_02;
	}
	else if (strcmp(device_name, "LAMP_03") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_LAMP_03;
	}
	else if (strcmp(device_name, "LAMP_04") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_LAMP_04;
	}
	else if (strcmp(device_name, "PUMP_01") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_PUMP_01;
	}
	else if (strcmp(device_name, "PUMP_02") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_PUMP_02;
	}
	else if (strcmp(device_name, "PUMP_03") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_PUMP_03;
	}
	else if (strcmp(device_name, "PUMP_04") == 0)
	{
		//target_device_gpio_num = GPIO_OUTPUT_PUMP_04;
	}
	else
	{
		ESP_LOGI(TAG, "No sub-device found!");
		return;
	} */
	if (strcmp(command, "TURN_ON") == 0)
	{
		//bat thiet bi neu no chua duoc bat, neu no da duoc bat roi thi khong lam gi
		printf(NOTIFICATION, device_name, "on");
	}
	else if (strcmp(command, "TURN_OFF") == 0)
	{
		//tat thiet bi no chua tat, neu no da tat roi thi khong lam gi
		printf(NOTIFICATION, device_name, "off");
	}
	else
	{
		ESP_LOGI(TAG, "Wrong command received!");
		return;
	}
}

void app_main(void) {
    ESP_LOGI(TAG, "[APP] Startup...");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    nvs_flash_init();
	
	esp_reset_reason_t reason = esp_reset_reason();
	if ((reason != ESP_RST_DEEPSLEEP) && (reason != ESP_RST_SW)) {
		isAuthenticated = false;
		time_sleep = 0;
		published_sensors_data_count = 0;
		key = 0;
	}
    adc1_config_width(ADC_WIDTH_10Bit);
    adc1_config_channel_atten(PH_SENSOR_PIN, ADC_ATTEN_DB_0);

    setDHTgpio(TEMP_HUMI_SENSOR_PIN);
    //Tao task nhan du lieu tu cac sensors
    xTaskCreate(&read_sensors_data_task, "get_sensors_data_task", 2048, NULL, 5, NULL);
	//Tao task tu dong khoi dong sau khi gui tin thanh cong
	//xTaskCreate(&restart_task, "do_restart_task", 2048, NULL, 5, NULL);
	
	/* gpio_config_t io_conf;
	//config gpio_config_t
	//disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf); */
	//xTaskCreate(&update_lamps_and_pumps_state_task, "update_device_state_task", 2048, NULL, 5, NULL);
    wifi_init();
}

