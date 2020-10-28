#define MQTTCLIENT_QOS2 1

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <memory.h>

using namespace std;

#include "MQTTClient.h"

#define DEFAULT_STACK_SIZE -1

#include "linux.cpp"

int arrivedcount = 0;

void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;

    printf("Message %d arrived: qos %d, retained %d, dup %d, packetid %d\n", 
		++arrivedcount, message.qos, message.retained, message.dup, message.id);
    printf("Payload %.*s\n", (int)message.payloadlen, (char*)message.payload);
}


int main(int argc, char* argv[])
{   
    IPStack ipstack = IPStack();
    float version = 0.3;
    const char* topic = "bme280_topic";
    
    printf("Version is %f\n", version);
              
    MQTT::Client<IPStack, Countdown> client = MQTT::Client<IPStack, Countdown>(ipstack);
    
    const char* hostname = "broker.hivemq.com";
    int port = 1883;
    printf("Connecting to %s:%d\n", hostname, port);
    int rc = ipstack.connect(hostname, port);
	if (rc != 0)
	    printf("rc from TCP connect is %d\n", rc);
 
	printf("MQTT connecting\n");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"bme280_client";
    rc = client.connect(data);
	if (rc != 0)
	    printf("rc from MQTT connect is %d\n", rc);
	printf("MQTT connected\n");
    
    rc = client.subscribe(topic, MQTT::QOS2, messageArrived);   
    if (rc != 0)
        printf("rc from MQTT subscribe is %d\n", rc);

    int limit = 10;
    //ifstream bme280_dev("/home/jeevansam/beagleboard/mqtt/bme280_misc");
    ifstream bme280_dev("/dev/bme280_misc");

    if(bme280_dev.is_open())
    {
        while(!bme280_dev.eof())
        {
            MQTT::Message message;

            // QoS 0
            char buf[100];
            bme280_dev >> buf;
            message.qos = MQTT::QOS0;
            message.retained = false;
            message.dup = false;
            message.payload = (void*)buf;
            message.payloadlen = strlen(buf)+1;
            rc = client.publish(topic, message);
            if (rc != 0)
                printf("Error %d from sending QoS 0 message\n", rc);
            else while (arrivedcount == 0)
                client.yield(100);
            if(limit-- <= 0)
                break;
            sleep(1);
        }
    }
    else
    {
        cout << "Unable to open the device file" << endl;
    }

    rc = client.unsubscribe(topic);
    if (rc != 0)
        printf("rc from unsubscribe was %d\n", rc);
    
    rc = client.disconnect();
    if (rc != 0)
        printf("rc from disconnect was %d\n", rc);
    
    ipstack.disconnect();
    
    printf("Finishing with %d messages received\n", arrivedcount);
    
    return 0;
}

