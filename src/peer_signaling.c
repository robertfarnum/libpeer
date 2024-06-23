#include <string.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <cJSON.h>

#include "config.h"
#include "base64.h"
#include "utils.h"
#include "peer_signaling.h"

#include "mqtt_client.h"

#define KEEP_ALIVE_TIMEOUT_SECONDS 60
#define CONNACK_RECV_TIMEOUT_MS 1000

#define JRPC_PEER_STATE "state"
#define JRPC_PEER_OFFER "offer"
#define JRPC_PEER_ANSWER "answer"
#define JRPC_PEER_CLOSE "close"

#define BUF_SIZE 4096
#define TOPIC_SIZE 128

typedef struct PeerSignaling {

  esp_mqtt_client_handle_t mqtt_client;
  char subtopic[TOPIC_SIZE];
  char pubtopic[TOPIC_SIZE];

  uint16_t packet_id;
  int id;
  PeerConnection *pc;

} PeerSignaling;

static PeerSignaling g_ps;

static void peer_signaling_mqtt_publish(const char *message) {
  int msg_id;
  LOGI("peer_signaling_mqtt_publish");
  msg_id = esp_mqtt_client_publish(g_ps.mqtt_client, g_ps.pubtopic, message, strlen(message), 0, 0);
  if (msg_id == -1) {

    LOGE("MQTT_Publish failed: Status=%d.", msg_id);
  } else {

    LOGD("MQTT_Publish succeeded.");
  }
}


static cJSON* peer_signaling_create_response(int id) {

  cJSON *json = cJSON_CreateObject();

  cJSON_AddStringToObject(json, "jsonrpc", "2.0");

  cJSON_AddNumberToObject(json, "id", id);

  return json;
}

static void peer_signaling_process_response(cJSON *json) {
  LOGI("peer_signaling_process_response");

  char *payload = cJSON_PrintUnformatted(json);

  if (payload) {

    peer_signaling_mqtt_publish(payload);

    free(payload);
  }

  cJSON_Delete(json);
}

static void peer_signaling_process_request(const char *msg, size_t size) {

  cJSON *request, *response = NULL;
  cJSON *method, *id;

  request = cJSON_Parse(msg);

  if (!request) {

    LOGW("Parse json failed: %s", msg);
    return;
  }

  do {

    method = cJSON_GetObjectItem(request, "method");
    
    id = cJSON_GetObjectItem(request, "id");
    
    if (!id && !cJSON_IsNumber(id)) {
      
      LOGW("Cannot find id");
      break;
    }

    if (!method && cJSON_IsString(method)) {

      LOGW("Cannot find method");
      break;
    }

    PeerConnectionState state = peer_connection_get_state(g_ps.pc);
    LOGI("STATE=%d", state);
    LOGI("METHOD=%s", method->valuestring);

    response = peer_signaling_create_response(id->valueint);

    if (strcmp(method->valuestring, JRPC_PEER_OFFER) == 0) {
      if (state == PEER_CONNECTION_CLOSED) { 

        g_ps.id = id->valueint;
        peer_connection_create_offer(g_ps.pc);
        cJSON_Delete(response);
        response = NULL;

      } else {

        cJSON_AddStringToObject(response, "result", "busy");
      }

    } else if (strcmp(method->valuestring, JRPC_PEER_ANSWER) == 0) {

      if (state == PEER_CONNECTION_NEW) {

        cJSON *params = cJSON_GetObjectItem(request, "params");
        
        if (!params && !cJSON_IsString(params)) {

          LOGW("Cannot find params");
          break;
        }

        peer_connection_set_remote_description(g_ps.pc, params->valuestring);
        cJSON_AddStringToObject(response, "result", "");
      }

    } else if (strcmp(method->valuestring, JRPC_PEER_STATE) == 0) {

       cJSON_AddStringToObject(response, "result", peer_connection_state_to_string(state));

    } else if (strcmp(method->valuestring, JRPC_PEER_CLOSE) == 0) {

      peer_connection_close(g_ps.pc);
      cJSON_AddStringToObject(response, "result", "");

    } else {

      LOGW("Unsupport method");
    }

  } while (0);

  if (response) {

    peer_signaling_process_response(response);
  }

  cJSON_Delete(request);
}

static int peer_signaling_mqtt_subscribe(int subscribed) {

  int msg_id;

  if (subscribed) {
    msg_id = esp_mqtt_client_subscribe(g_ps.mqtt_client, g_ps.subtopic, 0);
  } else {
    msg_id = esp_mqtt_client_unsubscribe(g_ps.mqtt_client, g_ps.subtopic);
  }
  if (msg_id == -1) {
    LOGE("MQTT_Subscribe failed: Status=%d.", msg_id);
    return -1;
  }

  LOGD("MQTT Subscribe/Unsubscribe succeeded.");
  return 0;
}

static void peer_signaling_onicecandidate(char *description, void *userdata) {
  LOGI("peer_signaling_onicecandidate");
#if CONFIG_MQTT
  cJSON *json = peer_signaling_create_response(g_ps.id);
  cJSON_AddStringToObject(json, "result", description);
  peer_signaling_process_response(json);
  g_ps.id = 0;
#endif
}



int peer_signaling_loop() {
  return 0;
}

void peer_signaling_leave_channel() {
  // TODO: HTTP DELETE with Location?
#if CONFIG_MQTT
  esp_err_t err = 0;

  if (peer_signaling_mqtt_subscribe(0) == 0) {
    err = esp_mqtt_client_disconnect(g_ps.mqtt_client);
  } 

  if(err == -1) {
    LOGE("Failed to disconnect with broker: %d", err);
  }
#endif
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    LOGD("Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    LOGD("free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        LOGI("MQTT_EVENT_CONNECTED");
        peer_signaling_mqtt_subscribe(1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        LOGI("MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        LOGI("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        LOGI("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        LOGI("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        LOGI("MQTT_EVENT_DATA");
        LOGI("TOPIC=%.*s", event->topic_len, event->topic);
        LOGI("DATA=%.*s", event->data_len, event->data);
        peer_signaling_process_request(event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        LOGI("MQTT_EVENT_ERROR");
        LOGI("MQTT5 return code is %d", event->error_handle->connect_return_code);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            LOGI("Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        LOGI("Other event id:%d", event->event_id);
        break;
    }
}

static int peer_signaling_mqtt_connect(const char *hostname, int port) {
    LOGI("BROKER_URI=%s", CONFIG_BROKER_URI);
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = CONFIG_BROKER_URI,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = true,
        // .credentials.client_id = "peer",
        // .credentials.username = "123",
        // .credentials.authentication.password = "456",
        .session.disable_clean_session = true,
        .session.keepalive = KEEP_ALIVE_TIMEOUT_SECONDS,
        .buffer.size = BUF_SIZE,
    };

  g_ps.mqtt_client = esp_mqtt_client_init(&mqtt5_cfg);

  /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
  esp_mqtt_client_register_event(g_ps.mqtt_client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);

  esp_err_t err;
  err = esp_mqtt_client_start(g_ps.mqtt_client);
  
  if (err == -1) {
    LOGE("MQTT_Connect failed: Status=%d.", err);
    return -1;
  }

  LOGI("MQTT_Connect succeeded.");
  return 0;
}

int peer_signaling_join_channel(const char *client_id, PeerConnection *pc) {
  LOGI("peer_signaling_join_channel");
  g_ps.pc = pc;
  peer_connection_onicecandidate(pc, peer_signaling_onicecandidate);

#if CONFIG_MQTT
  snprintf(g_ps.subtopic, sizeof(g_ps.subtopic), "webrtc/%s/jsonrpc", client_id);
  snprintf(g_ps.pubtopic, sizeof(g_ps.pubtopic), "webrtc/%s/jsonrpc-reply", client_id);
  peer_signaling_mqtt_connect(MQTT_HOST, MQTT_PORT);
#endif

  return 0;
}
