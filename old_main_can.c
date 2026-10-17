#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

  
#include <libwebsockets.h>
#include <json-c/json.h>

// Sensor Request
#define MASTER_REQUEST 0x0000FF01 // 01 is first follower 

// State extended IDs
#define INIT 0x00001000
#define FAULT 0x00001100
#define SAFE_TO_APPROACH 0x00001200
#define COAST 0x00001300
#define BRAKE 0x00001400
#define CRAWL 0x00001500
#define TRACK 0x00001600
#define LAUNCH 0x0000FF00
#define READY 0x00001700

// Number of followers
#define FOLLOWERS 1;

// For storing confirm responses from sending states
int state_responses[FOLLOWERS][2];  

// For storing all the sensor data after master request
int sensors[FOLLOWERS][4][20];

// Global flags to control sending
static int send_state_responses_flag = 0;
static int send_sensors_flag = 0;

// Function to map state strings to state IDs
unsigned int map_state_to_id(const char* state) {
    if (strcmp(state, "INIT") == 0) return INIT;
    else if (strcmp(state, "FAULT") == 0) return FAULT;
    else if (strcmp(state, "SAFE_TO_APPROACH") == 0) return SAFE_TO_APPROACH;
    else if (strcmp(state, "COAST") == 0) return COAST;
    else if (strcmp(state, "BRAKE") == 0) return BRAKE;
    else if (strcmp(state, "CRAWL") == 0) return CRAWL;
    else if (strcmp(state, "TRACK") == 0) return TRACK;
    else if (strcmp(state, "LAUNCH") == 0) return LAUNCH;
    else if (strcmp(state, "READY") == 0) return READY;
    else return 0;
}


// Function to serialize state responses
char* serialize_state_responses() {
    json_object *jroot = json_object_new_object();
    json_object *jstate_responses = json_object_new_array();

    for (int i = 0; i < FOLLOWERS; i++) {
        json_object *jresponse = json_object_new_array();
        json_object_array_add(jresponse, json_object_new_int(state_responses[i][0]));
        json_object_array_add(jresponse, json_object_new_int(state_responses[i][1]));
        json_object_array_add(jstate_responses, jresponse);
    }

    json_object_object_add(jroot, "state_responses", jstate_responses);
    const char *json_str = json_object_to_json_string(jroot);
    char *result = strdup(json_str);
    json_object_put(jroot);
    return result;
}

// Function to serialize sensor data
char* serialize_sensors() {
    json_object *jroot = json_object_new_object();
    json_object *jsensors = json_object_new_array();

    for (int i = 0; i < FOLLOWERS; i++) {
        json_object *jsensor_group = json_object_new_array();
        for (int j = 0; j < 4; j++) {
            json_object *jsensor_data = json_object_new_array();
            for (int k = 0; k < 20; k++) {
                json_object_array_add(jsensor_data, json_object_new_int(sensors[i][j][k]));
            }
            json_object_array_add(jsensor_group, jsensor_data);
        }
        json_object_array_add(jsensors, jsensor_group);
    }

    json_object_object_add(jroot, "sensors", jsensors);
    const char *json_str = json_object_to_json_string(jroot);
    char *result = strdup(json_str);
    json_object_put(jroot);
    return result;
}

// Function to create a socket (CAN bus)
int create_socket(const char* ifname) {
    struct sockaddr_can addr;
    struct ifreq ifr;
    int enable_canfd = 1;

    // Create a socket
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("Error while opening socket");
        return -1;
    }

    // Specify interface name
    strcpy(ifr.ifr_name, ifname);
    ioctl(s, SIOCGIFINDEX, &ifr);

    // Bind socket to network interface
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Error in socket bind"); 
        return -1;
    }

    // Enable BRS
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0) {
        perror("Error while enabling CAN FD for BRS"); 
        return -1;
    }

    return s;
}

// Function to send a state and wait for confirm
void send_state(int s, int current_state) {

    struct canfd_frame frame;
    
    // Can modify this for whatever we happens on the READY state
    if (current_state == READY){
        frame.can_id = current_state | CAN_EFF_FLAG;
        frame.can_dlc = 4;
        frame.data[0] = 0x00;
        frame.data[1] = 0x00;
        frame.data[2] = 0x00;
        frame.data[3] = 0x00;
    } else {
        frame.can_id = current_state | CAN_EFF_FLAG;
        frame.can_dlc = 1;
        frame.data[0] = 0x00;
    }

    if (write(s, &frame, sizeof(frame)) != sizeof(frame)){
        perror("Error in sending CAN frame"); 
    }

    read_state_responses(s);

}

// Function to read the responses and store data into state_responses array
void read_state_responses(int s) {

    struct canfd_frame frame_received;
    int responses_received = 0;
    int nbytes;

    printf("waiting for Can frame"); // to help with testing

    while (responses_received < FOLLOWERS){

        nbytes = read(s, &frame_received, sizeof(struct can_frame));

        if (nbytes == sizeof(struct can_frame)) {

            printf("Recieved response\n"); // to help with testing
            state_responses[responses_received][0] = frame_received.can_id;
            state_responses[responses_received][1] = frame_received.data[0];
            printf("Stored frame"); // to help with testing

            responses_received++;

        }
    }   
}

// Master sensor request function
void master_request(int s) {

    unsigned int request = MASTER_REQUEST;

    struct canfd_frame frame;

    for (int i = 0; i < FOLLOWERS; i++){

        frame.can_id = request | CAN_EFF_FLAG; // extended id flag
        frame.can_dlc = 1;
        frame.data[0] = 0x00;
   
        request++; // increment to get all followers

        if (write(s, &frame, sizeof(frame)) != sizeof(frame)){
            perror("Error in sending CAN frame");
        }

        // logic to read incoming packets and update global array
        
        struct canfd_frame frame_received;
        int responses_received = 0;
        int nbytes;

        printf("waiting for Can frame"); // to help with testing

       // Store data into sensors array
        for (int j = 0; j < 4; j++ ){    

            nbytes = read(s, &frame_received, sizeof(struct can_frame));
            
            if (nbytes == sizeof(struct can_frame)) {

                printf("Recieved response\n"); // to help with testing

                sensors[i][j][0] = frame_received.can_id;       // first index will be id
                
                for (int k = 0; k < frame_received.can_dlc; k++) {              
                    sensors[i][j][k+1] = frame_received.data[k];    // follower | sensor (packet) | data 
                }
                
                printf("Stored frame"); // to help with testing

            }
        }
    } 
}

// Callback function. Caleld by libwebsockets whenever an event occurs
static int callback_websockets(const struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_RECEIVE:
            // Copy the received message into the current_state variable
            // Data is pointed to byt in and has length len

            printf("Receievd state: %s\n", (char*) in);
            unsigned int state_id = map_state_to_id((char*) in);
            if (state_id != 0){
                // Send state to CAN0 immediately after receiving state
                send_state(can0, state_id);
                send_state_responses_flag = 1;
                lws_callback_on_writable(wsi); // Call the send as soon as its available
            } else {
                fprintf(stderr, "Unknown state recieved\n");
            }
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE: 
            if (send_state_responses_flag) {
                char *json_data = serialize_state_responses();
                lws_write(wsi, (unsigned char *) json_data, strlen(json_data), LWS_WRITE_TEXT);
                free(json_data);
                send_state_responses_flag = 0;
            }
            if (send_sensors_flag) {
                char *json_data = serialize_sensors();
                lws_write(wsi, (unsigned char *) json_data, strlen(json_data), LWS_WRITE_TEXT);
                free(json_data);
                send_sensors_flag = 0;
            }
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connection established\n");
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection failed\n");
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Connection closed\n");
            break;

        default:
            break;
    }
    return 0;
}

// Protocols Array, specifies protocols supported by the client
static const struct lws_protocols protocols[] = {
    {
        "default-protocol",
        callback_websockets,
        0,
        512, // Adjust the protocol buffer size as needed
    },
    { NULL, NULL, 0, 0 } /* terminator */
};


// main function, change as needed
int main() {

    // WebSocket initialization
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    struct lws_context *context = lws_create_context(&info);
    if (context == NULL) {
        fprintf(stderr, "lws init failed\n");
        return -1;
    }

    // Connecting to the server
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = context;
    connect_info.address = "localhost";
    connect_info.port = 3000;
    connect_info.path = "/";
    connect_info.protocol = protocols[0].name;
    connect_info.host = lws_canonical_hostname(context)

    struct lws *wsi = lws_client_connect_via_info(&connect_info);
    if (wsi == NULL) {
        fprintf(stderr, "Connection failed\n");
        lws_context_destroy(context);
        return -1;
    }

    // Create the 3 CANs
    int can0 = create_socket("can0"); // States
    int can1 = create_socket("can1"); // Sensor data
    int can2 = create_socket("can2"); // Internal

    time_t last_request_time = time(NULL);

    while(1){
        // Service WebSocket events and wait 1000ms. Returns negative on errors, 0 on event/no event.
        // Send state to can happens in callback function
        int n = lws_service(context, 1000);
        if (n < 0) { 
            fprintf(stderr, "Error occurred\n");
            break;
        }

        // send master request every 10 seconds
        if (time(NULL) - last_request_time >= 10) {
            // Send master request on can1
            master_request(can1);
            send_sensors_flag = 1;
            last_request_time = time(NULL);
            lws_callback_on_writable(wsi);
        }
    }

    // Close
    close(can0);
    close(can1);
    close(can2);
    lws_context_destroy(context);
    return 0;
}