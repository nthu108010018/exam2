#include "mbed.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "math.h"
#include "mbed_rpc.h"
#include "uLCD_4DGL.h"
#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "stm32l475e_iot01_accelero.h"

#define PI 3.1415
 


const char* host = "192.168.226.124";
int flag_1 = 1;
int flag_2 = 0;
int feature_arr[5] = {0};

Thread gui_thread(osPriorityNormal);
Thread angle_thread(osPriorityHigh);


volatile int message_num = 0;

volatile int arrivedcount = 0;

volatile bool closed = false;


const char* topic = "Mbed";


constexpr int kTensorArenaSize = 60 * 1024;

uint8_t tensor_arena[kTensorArenaSize];

struct Config config = {64, {20, 10, 5},  {
        "ring\n\r",
        "slope\n\r",
        "parrallel\n\r",
        }};

int sequence_number = 0;
DigitalIn btn2(USER_BUTTON);
BufferedSerial pc(USBTX, USBRX);
uLCD_4DGL uLCD(D1, D0, D2);






EventQueue gui_queue(8 * EVENTS_EVENT_SIZE);
EventQueue angle_queue(8 * EVENTS_EVENT_SIZE);



int model_deploy();
int PredictGesture(float* output);
void menu(int curr_op);
void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client, int index, int seq_num);
void messageArrived(MQTT::MessageData& md);

void model_run(Arguments *in, Reply *out);
void rpcClose1(Arguments *in, Reply *out);


DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);



RPCFunction modelrun(&model_run, "capture");
RPCFunction Close1(&rpcClose1, "close1");


WiFiInterface *wifi = WiFiInterface::get_default_instance();

NetworkInterface* net = wifi;

MQTTNetwork mqttNetwork(net);

MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);


int main(int argc, char* argv[]) {
   led1 = 0;
   led2 = 0;
   led3 = 0;

   

    if (!wifi) {

            printf("ERROR: No WiFiInterface found.\r\n");

            return -1;

    }



    printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);

    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);

    if (ret != 0) {

            printf("\nConnection error: %d\r\n", ret);

            return -1;

    }



    

    //TODO: revise host to your IP


    printf("Connecting to TCP network...\r\n");


    SocketAddress sockAddr;

    sockAddr.set_ip_address(host);

    sockAddr.set_port(1883);


    printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting


    int rc = mqttNetwork.connect(sockAddr);//(host, 1883);

    if (rc != 0) {

            printf("Connection error.");

            return -1;

    }

    printf("Successfully connected!\r\n");


    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

    data.MQTTVersion = 3;

    data.clientID.cstring = "Mbed";


    if ((rc = client.connect(data)) != 0){

            printf("Fail to connect MQTT\r\n");

    }

    if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){

            printf("Fail to subscribe\r\n");

    }
    char buf[256], outbuf[256];


    FILE *devin = fdopen(&pc, "r");

    FILE *devout = fdopen(&pc, "w");

    while(1) {

    memset(buf, 0, 256);

    for (int i = 0; ; i++) {

        char recv = fgetc(devin);

        if (recv == '\n') {

            printf("\r\n");

            break;

        }

        buf[i] = fputc(recv, devout);

    }

    //Call the static call method on the RPC class

    RPC::call(buf, outbuf);

    printf("%s\r\n", outbuf);

   }
   return 0;
}

int PredictGesture(float* output) {

  // How many times the most recent gesture has been matched in a row

  static int continuous_count = 0;

  // The result of the last prediction

  static int last_predict = -1;


  // Find whichever output has a probability > 0.8 (they sum to 1)

  int this_predict = -1;

  for (int i = 0; i < label_num; i++) {

    if (output[i] > 0.8) this_predict = i;

  }


  // No gesture was detected above the threshold

  if (this_predict == -1) {

    continuous_count = 0;

    last_predict = label_num;

    return label_num;

  }


  if (last_predict == this_predict) {

    continuous_count += 1;

  } else {

    continuous_count = 0;

  }

  last_predict = this_predict;


  // If we haven't yet had enough consecutive matches for this gesture,

  // report a negative result

  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {

    return label_num;

  }

  // Otherwise, we've seen a positive result, so clear all our variables

  // and report it

  continuous_count = 0;

  last_predict = -1;


  return this_predict;

}

int model_deploy(){
    
  
  // Whether we should clear the buffer next time we fetch data
  
  bool should_clear_buffer = false;

  bool got_data = false;


  // The gesture index of the prediction

  int gesture_index;


  // Set up logging.

  static tflite::MicroErrorReporter micro_error_reporter;

  tflite::ErrorReporter* error_reporter = &micro_error_reporter;


  // Map the model into a usable data structure. This doesn't involve any

  // copying or parsing, it's a very lightweight operation.

  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);

  if (model->version() != TFLITE_SCHEMA_VERSION) {

    error_reporter->Report(

        "Model provided is schema version %d not equal "

        "to supported version %d.",

        model->version(), TFLITE_SCHEMA_VERSION);

    return -1;

  }


  // Pull in only the operation implementations we need.

  // This relies on a complete list of all the ops needed by this graph.

  // An easier approach is to just use the AllOpsResolver, but this will

  // incur some penalty in code space for op implementations that are not

  // needed by this graph.
  
  static tflite::MicroOpResolver<6> micro_op_resolver;

  micro_op_resolver.AddBuiltin(

      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,

      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,

                               tflite::ops::micro::Register_MAX_POOL_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,

                               tflite::ops::micro::Register_CONV_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,

                               tflite::ops::micro::Register_FULLY_CONNECTED());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,

                               tflite::ops::micro::Register_SOFTMAX());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,

                               tflite::ops::micro::Register_RESHAPE(), 1);


  // Build an interpreter to run the model with

  static tflite::MicroInterpreter static_interpreter(

      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);

  tflite::MicroInterpreter* interpreter = &static_interpreter;


  // Allocate memory from the tensor_arena for the model's tensors

  interpreter->AllocateTensors();


  // Obtain pointer to the model's input tensor

  TfLiteTensor* model_input = interpreter->input(0);

  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||

      (model_input->dims->data[1] != config.seq_length) ||

      (model_input->dims->data[2] != kChannelNumber) ||

      (model_input->type != kTfLiteFloat32)) {

    error_reporter->Report("Bad input tensor parameters in model");

    return -1;

  }


  int input_length = model_input->bytes / sizeof(float);


  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);

  if (setup_status != kTfLiteOk) {

    error_reporter->Report("Set up failed\n");

    return -1;

  }


  error_reporter->Report("Set up successful...\n");


  while (true) {


    // Attempt to read new data from the accelerometer

    got_data = ReadAccelerometer(error_reporter, model_input->data.f,

                                 input_length, should_clear_buffer);


    // If there was no new data,

    // don't try to clear the buffer again and wait until next time

    if (!got_data) {

      should_clear_buffer = false;

      continue;

    }
    

    // Run inference, and report any error

    TfLiteStatus invoke_status = interpreter->Invoke();

    if (invoke_status != kTfLiteOk) {

      error_reporter->Report("Invoke failed on index: %d\n", begin_index);

      continue;

    }


    // Analyze the results to obtain a prediction

    gesture_index = PredictGesture(interpreter->output(0)->data.f);


    // Clear the buffer next time we read data

    should_clear_buffer = gesture_index < label_num;


    // Produce an output
    if (gesture_index < label_num) {

      error_reporter->Report(config.output_message[gesture_index]);
      menu(gesture_index);
      int temp;
      for(int i = 0; i<600; i++){
        
        temp = temp + abs(model_input->data.f[i]);

      }
      printf("%d\n", temp);
      int feature = 0;
      if(temp>200000){
        feature = 1;
      }
      feature_arr[sequence_number] = feature;
      sequence_number++;

      publish_message(&client, gesture_index, sequence_number);
    }
    /*if(!btn2){
        publish_message(&client, gesture_index, sequence_number);

    }*/
    
  }    
}

void model_run(Arguments *in, Reply *out){
  
    gui_queue.call(&model_deploy);
    gui_thread.start(callback(&gui_queue, &EventQueue::dispatch_forever));
  
    
    
}




void menu(int curr_op){
    uLCD.cls();
    const char *options[3] = { "RING", "SLOPE", 
                             "PARRALLEL" };

    uLCD.text_width(2);
    uLCD.text_height(2);

    uLCD.locate(0, 2);

    uLCD.printf(options[curr_op]);
}


void messageArrived(MQTT::MessageData& md) {

    MQTT::Message &message = md.message;

    char msg[300];

    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);

    printf(msg);

    ThisThread::sleep_for(1000ms);

    char payload[300];

    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);

    printf(payload);

    ++arrivedcount;

}

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client, int index, int seq_num) {


    MQTT::Message message;

    char buff[100];

    sprintf(buff, "gesture_index : %d  sequence_number: %d ", index,  seq_num);

    message.qos = MQTT::QOS0;

    message.retained = false;

    message.dup = false;

    message.payload = (void*) buff;

    message.payloadlen = strlen(buff) + 1;

    int rc = client->publish(topic, message);


    printf("rc:  %d\r\n", rc);

    printf("Puslish message: %s\r\n", buff);

}


void close_mqtt() {

    closed = true;

}

void rpcClose1(Arguments *in, Reply *out){
  gui_thread.terminate();
  for(int i=0; i<5; i++){
    publish_message(&client, feature_arr[i], -1);
  }
}

